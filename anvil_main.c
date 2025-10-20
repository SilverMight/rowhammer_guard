#define DEBUG

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/perf_event.h>
#include <linux/cpumask.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/err.h>
#include <linux/percpu.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/highmem.h>
#include <linux/sched.h>
#include <asm/page.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <asm/processor.h>

#include "anvil.h"
#include "dram_mapping.h"

#define MIN_SAMPLES     0
#define REFRESHED_ROWS  1

MODULE_LICENSE("GPL");

/* ============================== State ============================== */

static struct hrtimer sample_timer;
static ktime_t ktime;
static u64 old_val, val;
static u64 old_l1D_val, l1D_val, miss_total;

static sample_t sample_buffer[SAMPLES_MAX];
static int sampling;
static int start_sampling = 0;
static int sample_head;
static unsigned int sample_total;
static profile_t profile[PROFILE_N];
static unsigned int record_size;

static unsigned long L1_count = 0;
static unsigned long L2_count = 0;
static unsigned long refresh_count = 0;
static unsigned int hammer_threshold;
unsigned long dummy;

/* logging */
static struct sample_log log[25000];
static int log_index = 0;

/* workqueues */
static struct workqueue_struct *action_wq;
static struct workqueue_struct *llc_event_wq;
static struct work_struct task;
static struct work_struct task2;

/* perf events (per-cpu) */
DEFINE_PER_CPU(struct perf_event *, llc_event);
DEFINE_PER_CPU(struct perf_event *, l1D_event);
DEFINE_PER_CPU(struct perf_event *, ld_lat_event);
DEFINE_PER_CPU(struct perf_event *, precise_str_event);

/* fwd decls */
static void sort(void);
static void build_profile(void);
static int start_init(void);
static void finish_exit(void);

static void action_wq_callback(struct work_struct *work);
static void llc_event_wq_callback(struct work_struct *work);

static void llc_event_callback(struct perf_event *event,
                               struct perf_sample_data *data,
                               struct pt_regs *regs) { }

static void l1D_event_callback(struct perf_event *event,
                               struct perf_sample_data *data,
                               struct pt_regs *regs) { }

/* =================== GUP portability wrapper =================== */
/* 5.9+ commonly uses the 4-arg get_user_pages(start, nr, gup_flags, pages)
 * Older kernels may use 5-arg form. AlmaLinux 5.14 -> 4-arg.
 */
static unsigned long virt_to_phy(struct mm_struct *mm, unsigned long virt)
{
    unsigned long phys;
    struct page *pg = NULL;
    int ret;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,9,0)
    ret = get_user_pages(virt, 1, FOLL_WRITE, &pg);
#else
    ret = get_user_pages(virt, 1, FOLL_WRITE, &pg, NULL);
#endif

    if (ret <= 0)
        return 0;

    phys = page_to_phys(pg);
    return phys;
}

/* ============================ Perf callbacks ============================ */

static void precise_str_callback(struct perf_event *event,
                                 struct perf_sample_data *data,
                                 struct pt_regs *regs)
{
    /* record only stores to local DRAM (keep original heuristic) */
    if (data->data_src.val & (1ULL << 7)) {
        sample_buffer[sample_head].phy_page = virt_to_phy(current->mm, data->addr) >> 12;
        if (sample_buffer[sample_head].phy_page > 0) {
            sample_buffer[sample_head].addr = data->addr;
            if (++sample_head > SAMPLES_MAX - 1)
                sample_head = SAMPLES_MAX - 1;
            sample_total++;
        }
    }
}

static void load_latency_callback(struct perf_event *event,
                                  struct perf_sample_data *data,
                                  struct pt_regs *regs)
{
    sample_buffer[sample_head].phy_page = virt_to_phy(current->mm, data->addr) >> 12;

#ifdef DEBUG
    sample_buffer[sample_head].addr = data->addr;
    sample_buffer[sample_head].lat  = data->weight.full; /* cycles */
#endif

    if (++sample_head > SAMPLES_MAX - 1)
        sample_head = SAMPLES_MAX - 1;
    sample_total++;
}

/* ============================== Workqueue code ============================== */

static void llc_event_wq_callback(struct work_struct *work)
{
    int cpu;
    u64 enabled, running;
    u64 ld_miss;

    if (sampling) {
        /* stop sampling and analyze */
        for_each_online_cpu(cpu) {
            if (per_cpu(ld_lat_event, cpu))
                perf_event_disable(per_cpu(ld_lat_event, cpu));
            if (per_cpu(precise_str_event, cpu))
                perf_event_disable(per_cpu(precise_str_event, cpu));
        }
        sampling = 0;
        queue_work(action_wq, &task);
    } else if (start_sampling) {
        /* update LLC-load miss metric */
        l1D_val = 0;
        for_each_online_cpu(cpu) {
            if (per_cpu(l1D_event, cpu))
                l1D_val += perf_event_read_value(per_cpu(l1D_event, cpu), &enabled, &running);
        }

        ld_miss = l1D_val - old_l1D_val;

        /* pick what to sample based on LLC-load misses */
        if (ld_miss >= (miss_total * 9) / 10) {
            for_each_online_cpu(cpu) {
                if (per_cpu(ld_lat_event, cpu))
                    perf_event_enable(per_cpu(ld_lat_event, cpu));   /* loads only */
            }
        } else if (ld_miss < miss_total / 10) {
            for_each_online_cpu(cpu) {
                if (per_cpu(precise_str_event, cpu))
                    perf_event_enable(per_cpu(precise_str_event, cpu)); /* stores only (Intel only) */
            }
        } else {
            for_each_online_cpu(cpu) {
                if (per_cpu(ld_lat_event, cpu))
                    perf_event_enable(per_cpu(ld_lat_event, cpu));
                if (per_cpu(precise_str_event, cpu))
                    perf_event_enable(per_cpu(precise_str_event, cpu));
            }
        }

        sample_total = 0;
        record_size  = 0;
        sample_head  = 0;

        L1_count++;
        start_sampling = 0;
        sampling = 1;
    }

    old_l1D_val = l1D_val;
}

static void action_wq_callback(struct work_struct *work)
{
    int rec, log_;
    unsigned long pfn1, pfn2;
    unsigned long *virt;
    struct page *pg1, *pg2;
    int i;

    build_profile();
    sort();

#ifdef DEBUG
    log_ = 0;
#endif

    if (miss_total > LLC_MISS_THRESHOLD) {
        printk("samples = %u\n", sample_total);
        hammer_threshold = (LLC_MISS_THRESHOLD * sample_total) / miss_total;

        for (rec = 0; rec < record_size; rec++) {
#ifdef DEBUG
            profile[rec].hammer = 0;
#endif
            if ((profile[rec].llc_total_miss >= hammer_threshold / 2) &&
                (sample_total >= MIN_SAMPLES)) {
#ifdef DEBUG
                log_ = 1;
                profile[rec].hammer = 1;
                L2_count++;
#endif
                /* refresh adjacent rows */
                for (i = 1; i <= REFRESHED_ROWS; i++) {
                    pfn1 = dram_def->get_row_plus(profile[rec].phy_page, i);
                    pfn2 = dram_def->get_row_minus(profile[rec].phy_page, i);

                    pg1 = pfn_to_page(pfn1);
                    pg2 = pfn_to_page(pfn2);

                    virt = (unsigned long *)kmap(pg1);
                    if (virt) {
                        asm volatile("clflush (%0)" :: "r"(virt) : "memory");
                        get_user(profile[rec].dummy1, virt);
                        kunmap(pg1);
                    }

                    virt = (unsigned long *)kmap(pg2);
                    if (virt) {
                        asm volatile("clflush (%0)" :: "r"(virt) : "memory");
                        get_user(profile[rec].dummy2, virt);
                        kunmap(pg2);
                    }
                }
#ifdef DEBUG
                refresh_count++;
#endif
            }
        }
    }

#ifdef DEBUG
    if (log_) {
        for (rec = 0; rec < record_size; rec++) {
            log[log_index].profile[rec].phy_page = profile[rec].phy_page;
            log[log_index].profile[rec].llc_percent_miss = profile[rec].llc_percent_miss;
            log[log_index].profile[rec].dummy1 = profile[rec].dummy1;
            log[log_index].profile[rec].dummy2 = profile[rec].dummy2;
            log[log_index].profile[rec].hammer = profile[rec].hammer;
        }
        log[log_index].record_size  = record_size;
        log[log_index].sample_total = sample_total;
        log_index++;
        if (log_index > 24999)
            log_index = 24999;
    }
#endif
}

/* ============================== Timer callback ============================== */

static enum hrtimer_restart timer_callback(struct hrtimer *timer)
{
    ktime_t now;
    u64 enabled, running;
    int cpu;

    /* Update llc miss counter value */
    val = 0;
    for_each_online_cpu(cpu) {
        if (per_cpu(llc_event, cpu))
            val += perf_event_read_value(per_cpu(llc_event, cpu), &enabled, &running);
    }

    miss_total = val - old_val;
    old_val = val;

    if (!sampling) {
        if (miss_total > LLC_MISS_THRESHOLD) {
            start_sampling = 1;
            ktime = ktime_set(0, sample_timer_period);
            now = hrtimer_cb_get_time(timer);
            hrtimer_forward(&sample_timer, now, ktime);
        } else {
            ktime = ktime_set(0, count_timer_period);
            now = hrtimer_cb_get_time(timer);
            hrtimer_forward(&sample_timer, now, ktime);
        }
    } else {
        ktime = ktime_set(0, count_timer_period);
        now = hrtimer_cb_get_time(timer);
        hrtimer_forward(&sample_timer, now, ktime);
    }

    /* analyze LLC behavior in a wq */
    queue_work(llc_event_wq, &task2);
    return HRTIMER_RESTART;
}

/* ============================== Profile helpers ============================== */

static void build_profile(void)
{
    int rec, smpl, recorded;
    sample_t sample;

    if (sample_total > 0) {
        sample = sample_buffer[0];
        profile[0].phy_page         = sample.phy_page;
        profile[0].page             = (sample.addr);
        profile[0].llc_total_miss   = 1;
        profile[0].llc_percent_miss = 100;
        profile[0].cpu              = sample.cpu;
        record_size = 1;

        for (smpl = 1; smpl < sample_head; smpl++) {
            sample = sample_buffer[smpl];

            recorded = 0;
            for (rec = 0; rec < record_size; rec++) {
                if ((profile[rec].phy_page != 0) &&
                    (profile[rec].phy_page == sample.phy_page)) {
                    profile[rec].llc_total_miss++;
                    profile[rec].cpu = sample.cpu;
                    recorded = 1;
                    break;
                }
            }

            if (!recorded) {
                if (record_size < PROFILE_N) {
                    profile[record_size].phy_page       = sample.phy_page;
                    profile[record_size].page           = (sample.addr);
                    profile[record_size].llc_total_miss = 1;
                    profile[record_size].cpu            = sample.cpu;
                    record_size++;
                } else {
                    profile[record_size - 1].phy_page       = sample.phy_page;
                    profile[record_size - 1].page           = (sample.addr);
                    profile[record_size - 1].llc_total_miss = 1;
                    profile[record_size - 1].cpu            = sample.cpu;
                }
            }
        }

#ifdef DEBUG
        for (rec = 0; rec < record_size; rec++) {
            profile[rec].llc_percent_miss = (profile[rec].llc_total_miss * 100) / sample_total;
        }
#endif
    }
}

static void sort(void)
{
    int swapped, rec;
    do {
        swapped = 0;
        for (rec = 1; rec < record_size; rec++) {
            if (profile[rec - 1].llc_percent_miss < profile[rec].llc_percent_miss) {
                profile_t tmp = profile[rec - 1];
                profile[rec - 1] = profile[rec];
                profile[rec] = tmp;
                swapped = 1;
            }
        }
    } while (swapped);
}

/* ============================== Module init/exit ============================== */

static int start_init(void)
{
    int cpu;
    const struct cpuinfo_x86 *c = &boot_cpu_data;
    const struct perf_event_attr *load_attr;

    if (detect_and_register_dram_mapping()) {
        printk(KERN_ERR "anvil: Error detecting DRAM mapping\n");
        return -ENODEV;
    }

    /* LLC miss counter (generic) */
    old_val = 0;
    for_each_online_cpu(cpu) {
        per_cpu(llc_event, cpu) =
            perf_event_create_kernel_counter(&llc_miss_event, cpu,
                                             NULL, llc_event_callback, NULL);
        if (IS_ERR(per_cpu(llc_event, cpu))) {
            printk("anvil: Error creating llc event.\n");
            return 0;
        }
        perf_event_enable(per_cpu(llc_event, cpu));
    }

    /* Load-miss gauge (reuse generic cache misses as before) */
    old_l1D_val = 0;
    for_each_online_cpu(cpu) {
        per_cpu(l1D_event, cpu) =
            perf_event_create_kernel_counter(&l1D_miss_event, cpu,
                                             NULL, l1D_event_callback, NULL);
        if (IS_ERR(per_cpu(l1D_event, cpu))) {
            printk("anvil: Error creating l1D miss event.\n");
            return 0;
        }
        perf_event_enable(per_cpu(l1D_event, cpu));
    }

    /* Vendor-specific load latency event */
    if (c->x86_vendor == X86_VENDOR_AMD) {
        load_attr = &amd_load_latency_event;
        printk(KERN_INFO "anvil: Using AMD IBS-backed load latency event.\n");
    } else {
        load_attr = &intel_load_latency_event;
        printk(KERN_INFO "anvil: Using Intel load latency event.\n");
    }

    for_each_online_cpu(cpu) {
        per_cpu(ld_lat_event, cpu) =
            perf_event_create_kernel_counter(load_attr, cpu,
                                             NULL, load_latency_callback, NULL);
        if (IS_ERR(per_cpu(ld_lat_event, cpu))) {
            printk("anvil: Error creating load latency event.\n");
            return 0;
        }
    }

    /* Precise store: Intel only (AMD path skips; checks for NULL elsewhere) */
    if (c->x86_vendor != X86_VENDOR_AMD) {
        for_each_online_cpu(cpu) {
            per_cpu(precise_str_event, cpu) =
                perf_event_create_kernel_counter(&precise_str_event_attr, cpu,
                                                 NULL, precise_str_callback, NULL);
            if (IS_ERR(per_cpu(precise_str_event, cpu))) {
                printk("anvil: Error creating precise store event.\n");
                return 0;
            }
        }
    }

    /* Timer setup */
    ktime = ktime_set(0, count_timer_period);
    hrtimer_init(&sample_timer, CLOCK_REALTIME, HRTIMER_MODE_REL);
    sample_timer.function = &timer_callback;
    hrtimer_start(&sample_timer, ktime, HRTIMER_MODE_REL);

    /* Workqueues */
    action_wq = create_workqueue("action_queue");
    INIT_WORK(&task, action_wq_callback);

    llc_event_wq = create_workqueue("llc_event_queue");
    INIT_WORK(&task2, llc_event_wq_callback);

    printk("done initializing\n");
    return 0;
}

static void finish_exit(void)
{
    int cpu, i, j;

    hrtimer_cancel(&sample_timer);

    /* llc_event */
    for_each_online_cpu(cpu) {
        if (per_cpu(llc_event, cpu)) {
            perf_event_disable(per_cpu(llc_event, cpu));
            perf_event_release_kernel(per_cpu(llc_event, cpu));
            per_cpu(llc_event, cpu) = NULL;
        }
    }

    /* l1D_event */
    for_each_online_cpu(cpu) {
        if (per_cpu(l1D_event, cpu)) {
            perf_event_disable(per_cpu(l1D_event, cpu));
            perf_event_release_kernel(per_cpu(l1D_event, cpu));
            per_cpu(l1D_event, cpu) = NULL;
        }
    }

    /* load latency event */
    for_each_online_cpu(cpu) {
        if (per_cpu(ld_lat_event, cpu)) {
            perf_event_disable(per_cpu(ld_lat_event, cpu));
            perf_event_release_kernel(per_cpu(ld_lat_event, cpu));
            per_cpu(ld_lat_event, cpu) = NULL;
        }
    }

    /* precise store event */
    for_each_online_cpu(cpu) {
        if (per_cpu(precise_str_event, cpu)) {
            perf_event_disable(per_cpu(precise_str_event, cpu));
            perf_event_release_kernel(per_cpu(precise_str_event, cpu));
            per_cpu(precise_str_event, cpu) = NULL;
        }
    }

#ifdef DEBUG
    printk(">>>>>>>>>>>>>>>>log dump>>>>>>>>>>>>>>>\n");
    for (i = 0; i < log_index; i++) {
        for (j = 0; j < 4; j++) {
            printk("%lu,", log[i].profile[j].phy_page);
        }
        for (j = 0; j < 4; j++) {
            printk("%d,", log[i].profile[j].hammer);
            printk("0x%lx,", log[i].profile[j].dummy1);
            printk("0x%lx,", log[i].profile[j].dummy2);
        }
        printk("%u\n", log[i].sample_total);
    }
    printk("L1 count = %lu\n", L1_count);
    printk("L2 count = %lu\n", L2_count);
    printk("Refreshs = %lu\n", refresh_count);
    printk(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
#endif
}

/* Register init/exit */
module_init(start_init);
module_exit(finish_exit);

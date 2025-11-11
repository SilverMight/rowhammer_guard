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
#include <linux/sort.h>
#include <linux/log2.h>
#include <linux/mm_types.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>

#include "anvil.h"
#include "dram_mapping.h"
#include "anvil_sysfs.h"


#define MIN_SAMPLES 0
#define REFRESHED_ROWS 1

/* Default thresholds and timing (can be overridden via module parameters) */
unsigned int LLC_MISS_THRESHOLD = 20000;
module_param(LLC_MISS_THRESHOLD, uint, 0644);
MODULE_PARM_DESC(LLC_MISS_THRESHOLD, "Threshold of LLC misses before sampling starts");


MODULE_LICENSE("GPL");

static struct hrtimer sample_timer;
static ktime_t ktime;
static u64 old_val,val;
static u64 old_l1D_val,l1D_val,miss_total;

enum sampling_state {
	STATE_IDLE,
	STATE_ARMED,
	STATE_SAMPLING,
};

static enum sampling_state current_state = STATE_IDLE;
static DEFINE_SPINLOCK(sampling_lock);

static profile_t profile[PROFILE_N];
static unsigned int record_size;
static DEFINE_SPINLOCK(samples_lock);
static DECLARE_KFIFO(samples, sample_t, roundup_pow_of_two(SAMPLES_MAX));
/* counts number of times L1 threhold was
passed (sampling was done) */
unsigned long L1_count=0;
/* counts number of times hammering was detected */
unsigned long L2_count=0;
unsigned long refresh_count=0;
static unsigned int hammer_threshold;
unsigned long dummy;

/* for logging */
static struct sample_log log[25000];
static int log_index=0;

static struct workqueue_struct *action_wq;
static struct workqueue_struct *llc_event_wq;
static struct work_struct task;
static struct work_struct task2;

static void build_profile(size_t sample_total);
static int profile_compare(const void *a, const void *b);
DEFINE_PER_CPU(struct perf_event *, llc_event);
DEFINE_PER_CPU(struct perf_event *, l1D_event);
DEFINE_PER_CPU(struct perf_event *, ld_lat_event);
DEFINE_PER_CPU(struct perf_event *, precise_str_event);

void action_wq_callback( struct work_struct *work);
void llc_event_wq_callback( struct work_struct *work);

void llc_event_callback(struct perf_event *event,
            struct perf_sample_data *data,
            struct pt_regs *regs){}

void l1D_event_callback(struct perf_event *event,
            struct perf_sample_data *data,
            struct pt_regs *regs){}


/* convert virtual address from user process into physical address */
/* @input: mm - memory discriptor user process
   @input: virt - virtual address

   @return: corresponding physical address of "virt" */

static unsigned long virt_to_phy( struct mm_struct *mm,unsigned long virt)
{
	unsigned long phys;
	struct page *pg;
	int ret;

	mmap_read_lock(mm);

	ret = get_user_pages_remote (
		mm,
		virt,
		1,
		FOLL_GET,
		&pg,
        NULL,
		NULL
    );

	mmap_read_unlock(mm);

	if(ret <= 0) {
		pr_warn(KERN_WARNING "anvil: get_user_pages_remote failed for va: 0x%lx\n", virt);
		return 0;
	}

	/* get physical address */
    phys = page_to_phys(pg);
    // Release page, otherwise we will hold the page and never free it
    put_page(pg);

	return phys;
}

static unsigned long sample_to_pfn(sample_t* sample)
{
	unsigned long pfn;

	/* Translate if needed */
	if (sample->phy_page == 0 && sample->mm) {
		pfn = virt_to_phy(sample->mm, sample->virt_addr) >> PAGE_SHIFT;
		mmput(sample->mm); /* Release mm reference */
	} else {
		pfn = sample->phy_page >> PAGE_SHIFT;
	}

	return pfn;
}

static void store_sample(struct mm_struct* mm,
						 unsigned long virt_addr)
{
	sample_t sample;
	unsigned long flags;

	if (mm && mmget_not_zero(mm)) {
		sample.virt_addr = virt_addr;
		sample.phy_page = 0; // Mark for translation
		sample.mm = mm;
		sample.cpu = raw_smp_processor_id();

		spin_lock_irqsave(&samples_lock, flags);
		kfifo_put(&samples, sample);
		spin_unlock_irqrestore(&samples_lock, flags);
	}
}

/* Interrupt handler for store sample */
void precise_str_callback(struct perf_event *event,
            				struct perf_sample_data *data,
            				struct pt_regs *regs)
{
	/* Check source of store, if local dram (|0x80) record sample */
	if(data->data_src.val & (1<<7)){
		store_sample(current->mm, data->addr);
	}
}

/* Interrupt handler for load sample */
void load_latency_callback(struct perf_event *event,
            struct perf_sample_data *data,
            struct pt_regs *regs)
{	
	store_sample(current->mm, data->addr);
}

void llc_event_wq_callback(struct work_struct *work)
{
	int cpu;
	u64 enabled,running;
	u64 ld_miss;
	unsigned long flags;
	bool should_queue_work = false;

	spin_lock_irqsave(&sampling_lock, flags);
	switch (current_state) {
		case STATE_SAMPLING: {
			/* stop sampling */
			for_each_online_cpu(cpu){
				perf_event_disable(per_cpu(ld_lat_event,cpu));
				perf_event_disable(per_cpu(precise_str_event,cpu));
			}
			current_state = STATE_IDLE;
			should_queue_work = true;
			break;
		}
		case STATE_ARMED: {
			/* update MEM_LOAD_UOPS_MISC_RETIRED_LLC_MISS value */
			l1D_val = 0;
			for_each_online_cpu(cpu){
				l1D_val += perf_event_read_value(per_cpu(l1D_event,cpu), 
									 &enabled, &running);
			}

			ld_miss = l1D_val - old_l1D_val;

			// reset samples BEFORE enabling events
			kfifo_reset(&samples);
			record_size = 0;

			/* Sample loads, stores or both based on LLC load miss count */
			if(ld_miss >= (miss_total*9)/10){
				for_each_online_cpu(cpu){
					perf_event_enable(per_cpu(ld_lat_event,cpu));//sample loads only
				}
			}

			else if(ld_miss < miss_total/10){
				for_each_online_cpu(cpu){
					perf_event_enable(per_cpu(precise_str_event,cpu));//sample stores only
				}
			}

			else{
				for_each_online_cpu(cpu){
					/* sample both */
					perf_event_enable(per_cpu(ld_lat_event,cpu));
					perf_event_enable(per_cpu(precise_str_event,cpu));
				}			
			}


			/* log how many times we passed the threshold */
			L1_count++;
			current_state = STATE_SAMPLING;
			break;
		}
		default:
			break;
	}
	spin_unlock_irqrestore(&sampling_lock, flags);

	if (should_queue_work)
		queue_work(action_wq, &task);

	old_l1D_val = l1D_val;
}

/* look at sample profile and take action */
void action_wq_callback( struct work_struct *work)
{
	int rec,log_;
	unsigned long pfn1,pfn2;
	unsigned long *virt;
    size_t sample_total;

	struct page *pg1,*pg2;
	int i;
		
    /* NOTE: Any operations here do NOT need to lock samples_lock:
     * This workqueue is only queued after sampling is stopped,
     * so no other code is adding to the samples kfifo at this time. */

	/* Get number of samples before consuming them */
	sample_total = kfifo_len(&samples);

	/* group samples based on physical pages */
	build_profile(sample_total);
	/* sort profile, address with highest number
	of samples first */
    sort(profile,record_size,sizeof(profile_t),profile_compare,NULL);

#ifdef DEBUG
	log_=0;
#endif
	if(miss_total > LLC_MISS_THRESHOLD){//if still  high miss
#ifdef DEBUG
		printk("samples = %lu\n",sample_total);
#endif
	/* calculate hammer threshold */
        hammer_threshold = (LLC_MISS_THRESHOLD*sample_total)/miss_total;

        /* check for potential agressors */
        for(rec = 0;rec<record_size;rec++){
#ifdef DEBUG
            profile[rec].hammer = 0;
#endif
            if((profile[rec].llc_total_miss >= hammer_threshold/2) && (sample_total >= MIN_SAMPLES)){
#ifdef DEBUG
                log_ = 1;
                profile[rec].hammer = 1;
                L2_count++;
                printk("anvil: Potential hammering detected on page %lu with %lu misses\n",
                        profile[rec].phy_page,profile[rec].llc_total_miss);
#endif
                /* potential hammering detected , deploy refresh */
                for(i=1;i<=REFRESHED_ROWS;i++){
                    /* get page frame number for pages in rows above and below */
                    pfn1 = dram_def->get_row_plus(profile[rec].phy_page,i);
                    pfn2 = dram_def->get_row_minus(profile[rec].phy_page,i);

                    /* map pages to kernel space and refresh 
                     * ensure page is not reserved or offline
                    */
                    pg1 = pfn_to_online_page(pfn1);
                    if(pg1 && !PageReserved(pg1)) {
                        virt = (unsigned long*)kmap(pg1);
                        if(virt){
                            asm volatile("clflush (%0)"::"r"(virt):"memory");
                            profile[rec].dummy1 = READ_ONCE(*virt);
                            kunmap(pg1);
                        }
                    }

                    pg2 = pfn_to_online_page(pfn2);
                    if(pg2 && !PageReserved(pg2)) {
                        virt = (unsigned long*)kmap(pg2);
                        if(virt){
                            asm volatile("clflush (%0)"::"r"(virt):"memory");
                            profile[rec].dummy2 = READ_ONCE(*virt);
                            kunmap(pg2);
                        }
                    }
                }

            }
#ifdef DEBUG
            refresh_count++;
#endif
        }
    }

#ifdef DEBUG
	if(log_){
		for(rec = 0;rec<record_size;rec++){
			log[log_index].profile[rec].phy_page = profile[rec].phy_page;
			log[log_index].profile[rec].llc_percent_miss = profile[rec].llc_percent_miss;
			log[log_index].profile[rec].dummy1 = profile[rec].dummy1;
			log[log_index].profile[rec].dummy2 = profile[rec].dummy2;
			log[log_index].profile[rec].hammer = profile[rec].hammer;
		}
		log[log_index].record_size = record_size;
		log[log_index].sample_total = sample_total;
		log_index++;
		if(log_index > 24999)
			log_index = 24999;
	}
#endif
	return;
}

/* Timer interrupt handler */
enum hrtimer_restart timer_callback( struct hrtimer *timer )
{
	ktime_t now;
	u64 enabled,running;
	int cpu;
	unsigned long flags;
        
    /* Update llc miss counter value */
	val = 0;
	for_each_online_cpu(cpu){
    	val += perf_event_read_value(per_cpu(llc_event,cpu), &enabled, &running);
	}

	miss_total = val - old_val;
	old_val = val;

	spin_lock_irqsave(&sampling_lock, flags);
	if(current_state == STATE_IDLE){
	/* Start sampling if miss rate is high */
		if(miss_total > LLC_MISS_THRESHOLD){
			current_state = STATE_ARMED;
			/* set next interrupt interval for sampling */
			ktime = ktime_set(0,sample_timer_period);
      		now = hrtimer_cb_get_time(timer); 
      		hrtimer_forward(&sample_timer,now,ktime);
		}

		else{
			/* set next interrupt interval for counting */
			ktime = ktime_set(0,count_timer_period);
     		now = hrtimer_cb_get_time(timer); 
      		hrtimer_forward(&sample_timer,now,ktime);
		}
	}

	else{
		ktime = ktime_set(0,count_timer_period);
     	now = hrtimer_cb_get_time(timer); 
      	hrtimer_forward(&sample_timer,now,ktime);
	}
	spin_unlock_irqrestore(&sampling_lock, flags);
				
	/* start task that analyzes llc misses */
	queue_work(llc_event_wq, &task2);

	/* restart timer */
   	return HRTIMER_RESTART;
}

/* Groups samples accoriding to accessed physical pages */
static void build_profile(size_t sample_total)
{
	int rec;
	sample_t sample;
	unsigned long phy_page;

	while (kfifo_get(&samples, &sample)) {
		phy_page = sample_to_pfn(&sample); 
		if (!phy_page) {
			continue;
		}

		/* see if page already exists in profile */
		for (rec = 0; rec < record_size; rec++) {
			if (profile[rec].phy_page == phy_page) {
				profile[rec].llc_total_miss++;
				profile[rec].cpu = sample.cpu;
				break;
			}
		}

		if (rec == record_size) {
			/* new entry */
			if (record_size < PROFILE_N) {
				profile[record_size].phy_page = phy_page;
				profile[record_size].llc_total_miss = 1;
				profile[record_size].cpu = sample.cpu;
				record_size++;
			} else {
				/* overwrite last entry if full */
				profile[PROFILE_N - 1].phy_page = phy_page;
				profile[PROFILE_N - 1].llc_total_miss = 1;
				profile[PROFILE_N - 1].cpu = sample.cpu;
			}
		}
	}
#ifdef DEBUG
	if (sample_total > 0) {
		for(rec=0;rec<record_size;rec++){
			profile[rec].llc_percent_miss = (profile[rec].llc_total_miss*100)/sample_total;
		}
	}
#endif
}

/* Sort addresses with higest address distribution first */
static int profile_compare(const void *a, const void *b)
{
    profile_t *prof_a = (profile_t *)a;
    profile_t *prof_b = (profile_t *)b;

    // NOTE: sorting by total LLC misses
    return prof_b->llc_total_miss - prof_a->llc_total_miss;
}

/* Initialize module */
static int start_init(void)
{
	int cpu;
    int ret;

    INIT_KFIFO(samples);

	/* insert sysfs entry */
	ret = anvil_sysfs_init();
	if (ret) {
		printk(KERN_ERR "anvil: failed to initialize sysfs interface\n");
		return ret;
	}

    ret = detect_and_register_dram_mapping();
    if(ret){
            printk(KERN_ERR "Error detecting DRAM mapping\n");
            return ret;
    }

	old_val = 0;
	/* Setup LLC Miss event */
	for_each_online_cpu(cpu){
   		per_cpu(llc_event, cpu) = perf_event_create_kernel_counter(&llc_miss_event, cpu,
                 NULL,llc_event_callback,NULL);
   	 	if(IS_ERR(per_cpu(llc_event, cpu))){
        	printk("Error creating llc event.\n");
        	return 0;
    	}				
		/* start counting */
		perf_event_enable(per_cpu(llc_event, cpu));
	}

	old_l1D_val = 0;
	/* setup LLC Miss event */
	for_each_online_cpu(cpu){
   		per_cpu(l1D_event, cpu) = perf_event_create_kernel_counter(&l1D_miss_event, cpu,
                 NULL,l1D_event_callback,NULL);
   	 	if(IS_ERR(per_cpu(l1D_event, cpu))){
        	printk("Error creating l1D miss event.\n");
        	return 0;
    	}
						
		/* start counting */
		perf_event_enable(per_cpu(l1D_event, cpu));
	}

	/* setup load latency event */
	for_each_online_cpu(cpu){
   		per_cpu(ld_lat_event, cpu) = perf_event_create_kernel_counter(&load_latency_event, cpu,
                 													NULL,load_latency_callback,NULL);
   	 	if(IS_ERR(per_cpu(ld_lat_event, cpu))){
        	printk("Error creating load latency event.\n");
        	return 0;
    	}
	}

	/* setup precise store event */
	for_each_online_cpu(cpu){
   		per_cpu(precise_str_event, cpu) = perf_event_create_kernel_counter(&precise_str_event_attr, cpu,
                 															NULL,precise_str_callback,NULL);
   	 	if(IS_ERR(per_cpu(precise_str_event, cpu))){
        	printk("Error creating precise store event.\n");
        	return 0;
    	}
	}

	/* setup Timer */
    ktime = ktime_set(0,count_timer_period);
    hrtimer_init(&sample_timer,CLOCK_REALTIME,HRTIMER_MODE_REL);
    sample_timer.function = &timer_callback;
    hrtimer_start(&sample_timer,ktime,HRTIMER_MODE_REL);
     
	/* initialize work queue */
	action_wq = create_workqueue("action_queue");
	INIT_WORK(&task, action_wq_callback);

	llc_event_wq = create_workqueue("llc_event_queue");
	INIT_WORK(&task2, llc_event_wq_callback);

	printk("done initializing\n");
  	
   	return 0;
}

/* Cleanup module */
static void finish_exit(void)
{
    int ret,cpu,i,j; 
    /* timer */
    ret = hrtimer_cancel(&sample_timer);

    /* llc_event */
	for_each_online_cpu(cpu){
    	if(per_cpu(llc_event, cpu)){
    		perf_event_disable(per_cpu(llc_event, cpu));
       		perf_event_release_kernel(per_cpu(llc_event, cpu));
    	}
	}

	/* l1D_event */
	for_each_online_cpu(cpu){
    	if(per_cpu(l1D_event, cpu)){
    		perf_event_disable(per_cpu(l1D_event, cpu));
        	perf_event_release_kernel(per_cpu(l1D_event, cpu));
    	}
	}

	/* load latency event */
	for_each_online_cpu(cpu){
    	if(per_cpu(ld_lat_event, cpu)){
        	perf_event_disable(per_cpu(ld_lat_event, cpu));
        	perf_event_release_kernel(per_cpu(ld_lat_event, cpu));
    	}
	}

	/* precise store event */
	for_each_online_cpu(cpu){
    	if(per_cpu(precise_str_event, cpu)){
    		perf_event_disable(per_cpu(precise_str_event, cpu));
        	perf_event_release_kernel(per_cpu(precise_str_event, cpu));
   		 }
	}

	flush_workqueue(action_wq);
  	destroy_workqueue(action_wq);
	flush_workqueue(llc_event_wq);
  	destroy_workqueue(llc_event_wq);
	/* remove sysfs entry */
	anvil_sysfs_exit();

#ifdef DEBUG
	/* Log of ANVIL. CSV of some of the sampled/detected addresses */
			 
	printk(">>>>>>>>>>>>>>>>log dump>>>>>>>>>>>>>>>\n");
	/* dump all the logs */
	for(i=0; i<log_index; i++)
	{
		for(j=0; j<4; j++){
			/* physical pages */
			printk("%lu,",log[i].profile[j].phy_page);
		}

		for(j=0; j<4; j++){
			/* Values read form row above and row below */
			printk("%d,",log[i].profile[j].hammer);
			printk("0x%lx,",log[i].profile[j].dummy1);
			printk("0x%lx,",log[i].profile[j].dummy2);
		}
					
		/* Total samples per sample period */
		printk("%u\n",log[i].sample_total);
	}
	/* L1 count: Number of times LLC_MISS_THRESHOLD was crossed
	   L2 count: Number of times potential hammer activity was detected
	   Refresh: Number of addresses that resulted in refreshes */
	printk("L1 count = %lu\n",L1_count);
	printk("L2 count = %lu\n",L2_count);
	printk("Refreshs = %lu\n",refresh_count);
	printk(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
    return;
#endif
}

module_init(start_init);
module_exit(finish_exit);

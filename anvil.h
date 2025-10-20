#ifndef ANVIL_H
#define ANVIL_H

#include <linux/perf_event.h>

/* -----------------------------
 * Intel-specific RAW event codes
 * ----------------------------- */
#define LOAD_LATENCY_EVENT 0x01CD
#define PRECISE_STORE_EVENT 0x02CD
#define MEM_LOAD_UOPS_MISC_RETIRED_LLC_MISS 0x02D4

/* -----------------------------
 * Sampling & timer settings
 * ----------------------------- */
#define LD_LAT_SAMPLE_PERIOD 50
#define PRE_STR_SAMPLE_PERIOD 3000
#define count_timer_period 6000000
#define sample_timer_period 6000000
#define LLC_MISS_THRESHOLD 20000
#define PROFILE_N 20
#define SAMPLES_MAX 150

/* -----------------------------
 * LLC miss event (works on Intel & AMD)
 * ----------------------------- */
static struct perf_event_attr llc_miss_event = {
    .type = PERF_TYPE_HARDWARE,
    .config = PERF_COUNT_HW_CACHE_MISSES,
    .exclude_user = 0,
    .exclude_kernel = 1,
    .pinned = 1,
};

/* -----------------------------
 * L1D miss event (now generic cache misses)
 * ----------------------------- */
static struct perf_event_attr l1D_miss_event = {
    .type = PERF_TYPE_HARDWARE,
    .config = PERF_COUNT_HW_CACHE_MISSES,
    .exclude_user = 0,
    .exclude_kernel = 1,
    .pinned = 1,
};

/* -----------------------------------------------------------------
 * Intel latency & store sampling attributes
 * (AMD IBS attrs are built dynamically at runtime in anvil_main.c)
 * ----------------------------------------------------------------- */
static struct perf_event_attr load_latency_event = {
    .type = PERF_TYPE_RAW,
    .config = LOAD_LATENCY_EVENT,
    .config1 = 150,
    .sample_type = PERF_SAMPLE_ADDR | PERF_SAMPLE_DATA_SRC | PERF_SAMPLE_WEIGHT,
    .sample_period = LD_LAT_SAMPLE_PERIOD,
    .exclude_user = 0,
    .exclude_kernel = 1,
    .precise_ip = 1,
    .wakeup_events = 1,
    .disabled = 1,
    .pinned = 1,
};

static struct perf_event_attr precise_str_event_attr = {
    .type = PERF_TYPE_RAW,
    .config = PRECISE_STORE_EVENT,
    .sample_type = PERF_SAMPLE_ADDR | PERF_SAMPLE_DATA_SRC,
    .sample_period = PRE_STR_SAMPLE_PERIOD,
    .exclude_user = 0,
    .exclude_kernel = 1,
    .precise_ip = 1,
    .wakeup_events = 1,
    .disabled = 1,
    .pinned = 1,
};

/* -----------------------------
 * Data structures
 * ----------------------------- */
typedef struct {
    unsigned long phy_page;
    unsigned long page;
    int ld_st;
    unsigned long llc_total_miss;
    unsigned int llc_percent_miss;
    int cpu;
    unsigned long dummy1;
    unsigned long dummy2;
    int hammer;
} profile_t;

typedef struct {
    unsigned long phy_page;
    u64 addr;
    u64 lat;
    u64 time;
    unsigned int src;
    int ld_st;
    int cpu;
} sample_t;

struct sample_log {
    profile_t profile[20];
    unsigned int record_size;
    unsigned int sample_total;
    unsigned int hammer_threshold;
    int cpu;
};

#endif

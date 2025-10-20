#ifndef ANVIL_H
#define ANVIL_H

#include <asm/processor.h>  // for vendor detection
#include <linux/perf_event.h>

/* ============================================================
 * Intel-specific raw event codes
 * ============================================================ */
#define INTEL_LOAD_LATENCY_EVENT 0x01CD
#define INTEL_PRECISE_STORE_EVENT 0x02CD
#define INTEL_MEM_LOAD_UOPS_MISC_RETIRED_LLC_MISS 0x02D4

/* ============================================================
 * Intel-specific perf_event_attr definitions
 * ============================================================ */
static struct perf_event_attr intel_load_latency_event = {
    .type           = PERF_TYPE_RAW,
    .config         = INTEL_LOAD_LATENCY_EVENT,
    .config1        = 150,
    .sample_type    = PERF_SAMPLE_ADDR | PERF_SAMPLE_DATA_SRC | PERF_SAMPLE_WEIGHT,
    .sample_period  = 50,   /* LD_LAT_SAMPLE_PERIOD */
    .exclude_user   = 0,
    .exclude_kernel = 1,
    .precise_ip     = 1,
    .wakeup_events  = 1,
    .disabled       = 1,
    .pinned         = 1,
};

static struct perf_event_attr precise_str_event_attr = {
    .type           = PERF_TYPE_RAW,
    .config         = INTEL_PRECISE_STORE_EVENT,
    .sample_type    = PERF_SAMPLE_ADDR | PERF_SAMPLE_DATA_SRC,
    .sample_period  = 3000, /* PRE_STR_SAMPLE_PERIOD */
    .exclude_user   = 0,
    .exclude_kernel = 1,
    .precise_ip     = 1,
    .wakeup_events  = 1,
    .disabled       = 1,
    .pinned         = 1,
};

/* ============================================================
 * AMD: IBS-backed load latency event (precise_ip = 2)
 * ============================================================ */
static struct perf_event_attr amd_load_latency_event = {
    .type           = PERF_TYPE_HARDWARE,
    .config         = PERF_COUNT_HW_CPU_CYCLES,
    .sample_type    = PERF_SAMPLE_ADDR | PERF_SAMPLE_DATA_SRC | PERF_SAMPLE_WEIGHT,
    .sample_period  = 50,  /* LD_LAT_SAMPLE_PERIOD */
    .exclude_user   = 0,
    .exclude_kernel = 1,
    .precise_ip     = 2,
    .wakeup_events  = 1,
    .disabled       = 1,
    .pinned         = 1,
};

/* ============================================================
 * LLC and L1D miss events (generic, works on Intel & AMD)
 * ============================================================ */
static struct perf_event_attr llc_miss_event = {
    .type           = PERF_TYPE_HARDWARE,
    .config         = PERF_COUNT_HW_CACHE_MISSES,
    .exclude_user   = 0,
    .exclude_kernel = 1,
    .pinned         = 1,
};

static struct perf_event_attr l1D_miss_event = {
    .type           = PERF_TYPE_HARDWARE,
    .config         = PERF_COUNT_HW_CACHE_MISSES,
    .exclude_user   = 0,
    .exclude_kernel = 1,
    .pinned         = 1,
};

/* ============================================================
 * Sampling & threshold constants
 * ============================================================ */
#define LD_LAT_SAMPLE_PERIOD    50
#define PRE_STR_SAMPLE_PERIOD   3000
#define count_timer_period      6000000
#define sample_timer_period     6000000
#define LLC_MISS_THRESHOLD      20000
#define PROFILE_N               20
#define SAMPLES_MAX             150

/* ============================================================
 * Data structures
 * ============================================================ */
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

#endif /* ANVIL_H */

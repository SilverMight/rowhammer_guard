
#include <linux/perf_event.h>
#include <linux/kfifo.h>
#include "linux/mm_types.h"


#define LOAD_LATENCY_EVENT 0x01CD
#define PRECISE_STORE_EVENT 0x02CD
#define MEM_LOAD_UOPS_MISC_RETIRED_LLC_MISS 0x02D4

extern unsigned int llc_miss_threshold;

/* controls load sampling rate */
extern unsigned int ld_lat_sample_period;

/* controls store sampling rate */
extern unsigned int pre_str_sample_period;

/* count period in nanoseconds */
extern unsigned int count_timer_period;

/* sample period  in nanoseconds */
extern unsigned int sample_timer_period;

/* Maximum number of addresses in the address profile */
#define PROFILE_N 20

/* Maximum number of samples */
#define SAMPLES_MAX 150

/* LLC miss event attribute */
extern struct perf_event_attr llc_miss_event;

/* Load uops that misses LLC */
extern struct perf_event_attr l1D_miss_event;

/* Load latency event attribute */
extern struct perf_event_attr load_latency_event;

/*precise store event*/
extern struct perf_event_attr precise_str_event_attr;

/* Address profile */
typedef struct{
	unsigned long phy_page;
	struct mm_struct *mm;
	unsigned long page;
	int ld_st;
	unsigned long llc_total_miss;
	unsigned int llc_percent_miss;
	int cpu;
unsigned long dummy1;
unsigned long dummy2;
int hammer;
} profile_t;

/* Address sample */
typedef struct{
	unsigned long phy_page;
	struct mm_struct *mm;
	u64 virt_addr;
	u32 cpu;
}sample_t;

/* for logging */
struct sample_log{
	profile_t profile[20];
	unsigned int record_size;
	unsigned int sample_total;
	unsigned int hammer_threshold;
	int cpu;
};



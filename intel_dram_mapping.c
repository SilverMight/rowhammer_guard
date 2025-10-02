// Intel DRAM specific mapping
// Sandy Bridge and later? TODO: figure this out
#include <linux/cpufeature.h>
#include <linux/kernel.h>

#include "dram_mapping.h"
#include "linux/export.h"

/* Intel-specific implementations */
static unsigned long intel_get_bank(unsigned long pfn)
{
    return ((pfn >> 2) & 7) ^ ((pfn >> 6) & 7);
}

static unsigned long intel_get_row(unsigned long pfn)
{
    return pfn >> 6;
}

static unsigned long intel_get_column(unsigned long pfn)
{
    return (pfn >> 0) & 0x3F; /* Lower 6 bits for column */
}

static unsigned long intel_get_rank(unsigned long pfn)
{
    return (pfn >> 7) & 1;
}

static unsigned long intel_get_row_plus(unsigned long pfn, int inc)
{
    unsigned long bank_old = intel_get_bank(pfn);
    unsigned long row_new = (pfn >> 6) + inc;
    unsigned long bank_new = (row_new & 0x7) ^ bank_old;
    unsigned long rank_new = (pfn >> 7) & 1;

    return (row_new << 6) | (rank_new << 5) | (bank_new << 2) | (pfn & 0x3);
}

static unsigned long intel_get_row_minus(unsigned long pfn, int dec)
{
    unsigned long bank_old = intel_get_bank(pfn);
    unsigned long row_new = (pfn >> 6) - dec;
    unsigned long bank_new = (row_new & 0x7) ^ bank_old;
    unsigned long rank_new = (pfn >> 7) & 1;

    return (row_new << 6) | (rank_new << 5) | (bank_new << 2) | (pfn & 0x3);
}

static int intel_get_victim_pages(unsigned long aggressor_pfn,
                                  unsigned long* victim_pfns,
                                  int max_victims)
{
    int count = 0;
    int i;

    /* Get adjacent rows (typical rowhammer pattern) */
    for (i = 1; i <= max_victims / 2 && count < max_victims; i++) {
        if (count < max_victims) {
            victim_pfns[count++] = intel_get_row_plus(aggressor_pfn, i);
        }
        if (count < max_victims) {
            victim_pfns[count++] = intel_get_row_minus(aggressor_pfn, i);
        }
    }

    return count;
}

static bool intel_is_address_mappable(unsigned long pfn)
{
    return pfn > 0 && pfn < (1UL << 36); /* 36-bit physical addressing */
}

struct dram_mapping intel_dram_mapping = {
    .get_bank = intel_get_bank,
    .get_row = intel_get_row,
    .get_column = intel_get_column,
    .get_rank = intel_get_rank,
    .get_row_plus = intel_get_row_plus,
    .get_row_minus = intel_get_row_minus,
    .get_victim_pages = intel_get_victim_pages,
    .is_address_mappable = intel_is_address_mappable,
    .arch_name = "Intel (Sandy Bridge and later)",
};

EXPORT_SYMBOL(intel_dram_mapping);

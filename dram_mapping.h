#ifndef DRAM_MAPPING_H
#define DRAM_MAPPING_H

#include <linux/types.h>

struct dram_mapping {
    unsigned long (*get_bank)(unsigned long pfn);
    unsigned long (*get_row)(unsigned long pfn);
    unsigned long (*get_column)(unsigned long pfn);
    unsigned long (*get_rank)(unsigned long pfn);

    unsigned long (*get_row_plus)(unsigned long pfn, int inc);

    unsigned long (*get_row_minus)(unsigned long pfn, int dec);
    int (*get_victim_pages)(unsigned long aggressor_pfn, 
                           unsigned long *victim_pfns, 
                           int max_victims);
    bool (*is_address_mappable)(unsigned long pfn);

    const char *arch_name;
};

extern struct dram_mapping *dram_def;
extern struct dram_mapping intel_dram_mapping;

int register_dram_mapping(struct dram_mapping *mapping);
int detect_and_register_dram_mapping(void);

#endif // DRAM_MAPPING_H

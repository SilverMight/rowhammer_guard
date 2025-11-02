#ifndef DRAM_MAPPING_H
#define DRAM_MAPPING_H

#include <linux/types.h>

/* ------------------------------------------------------------------
 * DRAM mapping operations interface
 * ------------------------------------------------------------------ */
struct dram_mapping_ops {
    size_t (*get_bank)(size_t pfn);
    size_t (*get_row)(size_t pfn);
    size_t (*get_column)(size_t pfn);
    size_t (*get_rank)(size_t pfn);

    size_t (*get_row_plus)(size_t pfn, int inc);
    size_t (*get_row_minus)(size_t pfn, int dec);

    const char *arch_name;
};

/* ------------------------------------------------------------------
 * DRAM mapping configuration descriptor
 * ------------------------------------------------------------------ */
struct dram_config {
    const char* name;
    size_t phys_dram_offset;
    size_t matrix_size;

    /* Masks and shifts to decode DRAM address bits */
    size_t bank_mask;
    size_t bank_shift;
    size_t row_mask;
    size_t row_shift;
    size_t column_mask;
    size_t column_shift;

    const size_t* dram_matrix; /* phys -> dram */
    const size_t* addr_matrix; /* dram -> phys */
};

extern struct dram_mapping_ops *dram_def;

/* ------------------------------------------------------------------
 * Known configurations (defined in separate source files)
 * ------------------------------------------------------------------ */
extern struct dram_config intel_cometlake_config;

/* Optional: will be defined once Zen2 mapping is added */
extern struct dram_config amd_zen2_config;

/* ------------------------------------------------------------------
 * Registration and detection
 * ------------------------------------------------------------------ */
int register_dram_mapping(struct dram_mapping_ops *mapping);
int detect_and_register_dram_mapping(void);

#endif /* DRAM_MAPPING_H */

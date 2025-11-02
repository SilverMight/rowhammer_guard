#include "dram_mapping.h"

#include <asm/processor.h>
#include <linux/kernel.h>
#include <linux/module.h>

struct dram_mapping_ops* dram_def = NULL;
EXPORT_SYMBOL(dram_def);

static const struct dram_config* active_config = NULL;

#define PFN_TO_PHYS(pfn) ((size_t)(pfn) << 12)
#define PHYS_TO_PFN(phys) ((size_t)(phys) >> 12)

/* --------------------------------------------------------------
 * Helper: Apply XOR matrix to linearize DRAM address
 * -------------------------------------------------------------- */
static size_t apply_matrix(const size_t* matrix, unsigned int size, size_t addr) {
    size_t result = 0;
    int i;
    for (i = 0; i < size; ++i) {
        result <<= 1;
        result |= (hweight_long(matrix[i] & addr) & 1);
    }
    return result;
}

static size_t generic_get_linearized_addr(size_t pfn) {
    return apply_matrix(active_config->dram_matrix, active_config->matrix_size, PFN_TO_PHYS(pfn));
}

static size_t generic_get_bank(size_t pfn) {
    size_t linearized = generic_get_linearized_addr(pfn);
    return (linearized >> active_config->bank_shift) & active_config->bank_mask;
}

static size_t generic_get_row(size_t pfn) {
    size_t linearized = generic_get_linearized_addr(pfn);
    return (linearized >> active_config->row_shift) & active_config->row_mask;
}

static size_t generic_get_column(size_t pfn) {
    size_t linearized = generic_get_linearized_addr(pfn);
    return (linearized >> active_config->column_shift) & active_config->column_mask;
}

static size_t generic_get_rank(size_t pfn) {
    return 0;
}

static size_t get_pfn_from_dram_coords(size_t bank, size_t row, size_t col) {
    size_t linearized_addr =
        ((bank & active_config->bank_mask) << active_config->bank_shift) |
        ((row & active_config->row_mask) << active_config->row_shift) |
        ((col & active_config->column_mask) << active_config->column_shift);
    size_t phys_addr = apply_matrix(active_config->addr_matrix, active_config->matrix_size, linearized_addr);
    return PHYS_TO_PFN(phys_addr);
}

static size_t generic_get_row_plus(size_t pfn, int inc) {
    return get_pfn_from_dram_coords(generic_get_bank(pfn), generic_get_row(pfn) + inc, generic_get_column(pfn));
}

static size_t generic_get_row_minus(size_t pfn, int dec) {
    return get_pfn_from_dram_coords(generic_get_bank(pfn), generic_get_row(pfn) - dec, generic_get_column(pfn));
}

static struct dram_mapping_ops generic_dram_ops = {
    .get_bank = generic_get_bank,
    .get_row = generic_get_row,
    .get_column = generic_get_column,
    .get_rank = generic_get_rank,
    .get_row_plus = generic_get_row_plus,
    .get_row_minus = generic_get_row_minus,
};

/* --------------------------------------------------------------
 * Detect CPU and register mapping
 * -------------------------------------------------------------- */
int detect_and_register_dram_mapping(void)
{
    struct cpuinfo_x86 *c = &boot_cpu_data;

    /* Intel mappings */
    if (c->x86_vendor == X86_VENDOR_INTEL && c->x86 == 6) {
        switch (c->x86_model) {
            case 0xA5: /* Comet Lake */
            case 0xA6:
                active_config = &intel_cometlake_config;
                break;
        }
    }

    /* AMD Zen 2 — placeholder for future support */
    else if (c->x86_vendor == X86_VENDOR_AMD && c->x86 == 0x17) {
        /*
         * TODO: Add amd_zen2_config when ready:
         * active_config = &amd_zen2_config;
         *
         * For now, we’ll use fallback mapping.
         */
    }

    /* --------------------------------------------------------------
     * Fallback path if no known mapping found
     * -------------------------------------------------------------- */
    if (!active_config) {
        static const size_t id_matrix[1] = { 1 };
        static struct dram_config fallback = {
            .name = "Unknown-DRAM-Fallback",
            .dram_matrix = id_matrix,
            .addr_matrix = id_matrix,
            .phys_dram_offset = 0,
            .bank_mask = 0,
            .bank_shift = 0,
            .row_mask = 0,
            .row_shift = 0,
            .column_mask = 0,
            .column_shift = 0,
            .matrix_size = 1,
        };
        active_config = &fallback;
        generic_dram_ops.arch_name = active_config->name;
        dram_def = &generic_dram_ops;
        printk(KERN_WARNING "anvil: No known DRAM mapping; using fallback. Row+/- will be approximate.\n");
        return 0;
    }

    generic_dram_ops.arch_name = active_config->name;
    dram_def = &generic_dram_ops;
    printk(KERN_INFO "anvil: Detected and registered mapping for %s\n", active_config->name);
    return 0;
}

EXPORT_SYMBOL(detect_and_register_dram_mapping);

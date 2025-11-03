#include "dram_mapping.h"

#include <asm/processor.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/page_types.h>

struct dram_mapping_ops* dram_def = NULL;
EXPORT_SYMBOL(dram_def);

static const struct dram_config* active_config = NULL;

#define PFN_TO_PHYS(pfn) ((size_t)(pfn) << PAGE_SHIFT)
#define PHYS_TO_PFN(phys) ((size_t)(phys) >> PAGE_SHIFT)

// 
// GENERIC XOR-BASED DRAM MAPPING
//
static size_t apply_matrix(const size_t* matrix, unsigned int size, size_t addr) {
    size_t result = 0;
    int i;
    for (i = 0; i < size; ++i) {
        result <<= 1;
        // hweight_long & 1 is equivalent to __builtin_parityl
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

int detect_and_register_dram_mapping(void)
{

        struct cpuinfo_x86 *c = &boot_cpu_data;

    if (c->x86_vendor == X86_VENDOR_INTEL && c->x86 == 6) {
        switch (c->x86_model) {
            case 0x9E: // Coffee Lake
            case 0x97: 
                // TODO: add coffee lake config
                // active_config = &intel_coffeelake_config;
                break;
            case 0xA5: // Comet Lake
            case 0xA6:
                active_config = &intel_cometlake_config;
                break;
        }
    }
    // TODO: Add AMD support
    /* else if (c->x86_vendor == X86_VENDOR_AMD && c->x86 == 0x17) {
        // Family 0x17 is Zen. Now check model.
        switch (c->x86_model) {
            case 0x71: // Zen 2 (e.g., Ryzen 5 3600)
            case 0x60: // Zen 2 (e.g., Ryzen 5 5600G)
                active_config = &amd_zen2_config;
                break;
        }
    }
    */

    if (active_config) {
        generic_dram_ops.arch_name = active_config->name;
        dram_def = &generic_dram_ops;
        printk(KERN_INFO "anvil: Detected and registered mapping for %s\n", active_config->name);
        return 0;
    } else {
        printk(KERN_WARNING "anvil: No known DRAM mapping for this CPU. Functionality will be limited.\n");
        return -ENODEV;
    }
}

EXPORT_SYMBOL(detect_and_register_dram_mapping);

#include "dram_mapping.h"

#include <asm/processor.h>
#include <linux/kernel.h>
#include <linux/module.h>

struct dram_mapping* dram_def = NULL;
EXPORT_SYMBOL(dram_def);

enum DRAM_Mapping_Arch
{
    DRAM_ARCH_UNKNOWN = 0,
    DRAM_ARCH_INTEL,
    DRAM_ARCH_AMD,
};

int register_dram_mapping(struct dram_mapping* ops)

{
    dram_def = ops;

    printk(KERN_INFO "Registered DRAM geometry ops: %s\n",
           ops->arch_name ? ops->arch_name : "Unknown");

    return 0;
}

EXPORT_SYMBOL(register_dram_mapping);

static enum DRAM_Mapping_Arch detect_cpu_vendor(void)
{
    struct cpuinfo_x86* c = &boot_cpu_data;

    // TODO: check for correctness, and CPU microarchitecture
    switch (c->x86_vendor) {
        case X86_VENDOR_INTEL:
            return DRAM_ARCH_INTEL;
        case X86_VENDOR_AMD:
            return DRAM_ARCH_AMD;
        default:
            return DRAM_ARCH_UNKNOWN;
    }
}

int detect_and_register_dram_mapping(void)
{
    int vendor = detect_cpu_vendor();

    switch (vendor) {
        case DRAM_ARCH_INTEL:
            return register_dram_mapping(&intel_dram_mapping);
        case DRAM_ARCH_AMD:  // TODO: Implement AMD Mapping
        default:
            printk(KERN_ERR
                   "Unsupported CPU vendor for DRAM geometry mapping\n");
            return -ENODEV;
    }
}

EXPORT_SYMBOL(detect_and_register_dram_mapping);

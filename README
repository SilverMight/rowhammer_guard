# ANVIL: A Rowhammer Detection and Mitigation Kernel Module

**ANVIL** is a Linux loadable kernel module that leverages hardware performance counters to detect Rowhammer activity and protect vulnerable DRAM rows. When suspicious memory-access patterns are detected—based on cache-miss behavior and sampled load/store addresses—ANVIL proactively refreshes potential victim rows to prevent bit flips.

ANVIL has been tested on **Intel systems running Linux kernel 5.1.15**.

---

## Overview

ANVIL performs Rowhammer detection in two stages:

1. **Monitoring Phase**  
   The module periodically measures last-level cache (LLC) misses. If LLC activity exceeds `llc_miss_threshold`, address sampling is triggered.

2. **Sampling Phase**  
   During a short sampling window, ANVIL collects load/store samples using performance counter interrupts. These addresses are mapped to DRAM rows to identify potential aggressor pages. Rows identified as heavily accessed are used to infer potential victim rows, which ANVIL then refreshes by selectively reading from them.

---

## Module Parameters

ANVIL provides several tunable parameters that allow balancing detection accuracy and system overhead.  
Parameters can be set at module insertion time, for example:

    insmod anvil.ko llc_miss_threshold=10000

### **llc_miss_threshold**
- **Description:** LLC miss threshold for triggering the sampling phase.  
- **Effect:** Lower values increase detection accuracy but trigger sampling more often, increasing overhead for benign applications.  
- **Default:** `20000`

### **count_timer_period**
- **Description:** Duration (ns) over which LLC misses are counted during the monitoring phase.  
- **Default:** `6 ms`  
- **Notes:** Reducing this value speeds up detection.

### **sample_timer_period**
- **Description:** Duration (ns) of the sampling phase in which load/store samples are collected.  
- **Default:** `6 ms`  
- **Notes:** A shorter sampling period may require increased sampling rates to collect sufficient data.

### **ld_lat_sample_period**
- **Description:** Sampling rate for load instructions. Lower values increase sampling frequency.  
- **Default:** `50`  
- **Notes:** Very low values generate high interrupt rates and may negatively impact system performance.

### **pre_str_sample_period**
- **Description:** Sampling rate for store instructions.  
- **Default:** `3000`  
- **Notes:** Higher than the load sampling rate because precise-store events fire on all stores; the module filters LLC-store-misses in software.

### **aggressor_threshold_percentage**
- **Description:** Percentage threshold (1–100%) for flagging a memory page as a Rowhammer aggressor.  
- **Default:** `50%`  
- **Effect:** Lower thresholds increase sensitivity but may generate false positives.


## sysfs interface

ANVIL exposes runtime statistics and control options via the sysfs interface at `/sys/kernel/anvil/`.
- **`L1_count`**: Number of times llc_miss_threshold was exceeded.
- **`L2_count`**: Number of times Rowhammer activity was detected on a page.
- **`refresh_count`**: Total number of refreshes performed.

---

## Building and Running

### **Build the module**
    
    make

### **Insert the module**
    
    insmod anvil.ko

### **Remove the module**
    
    rmmod anvil.ko

---

## DRAM Memory Mapping

ANVIL includes a flexible DRAM address-mapping layer supporting both Intel and AMD processors. The module automatically detects the processor vendor and applies the correct mapping logic. This enables ANVIL to translate sampled physical addresses into DRAM rows—information essential for identifying aggressors and potential victim rows.

**Currently supported CPU microarchitectures:**
- **Intel Comet Lake**

To add support for a new architecture, extend the mapping logic in  
`dram_mapping.h`.

---

## Compatibility Notes

The module is validated on **Intel Comet Lake**.  
For newer Intel microarchitectures, you may need to update performance-event codes defined in `anvil.h`:

- `LOAD_LATENCY_EVENT`  
- `PRECISE_STORE_EVENT`  
- `MEM_LOAD_UOPS_MISC_RETIRED_LLC_MISS`

These values can be found in the **Intel® 64 and IA-32 Architectures Software Developer’s Manual**.

**AMD processors:**  
AMD uses a different sampling architecture, so supporting AMD systems may require more extensive code modifications.

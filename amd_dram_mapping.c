// AMD mapping from Zenhammer repo for a AMD zen 2, rank: 1, bank grouos: 4, and baks: 4 CPU
#include <linux/cpufeature.h>
#include <linux/kernel.h>

#include "dram_mapping.h"
#include "linux/export.h"



static const size_t zen2_dram_matrix[] = {
        0b00100010001010000000000000000,  // bg_b1 = addr b26 b22 b18 b16
        0b00010001000101000000000000000,  // bg_b0 = addr b25 b21 b17 b15
        0b10001000100000100000000000000,  // ba_b1 = addr b28 b24 b20 b14
        0b01000100010000011111111000000,  // ba_b0 = addr b27 b23 b19 b13 b12 b11 b10 b9 b8 b7 b6
        0b10000000000000000000000000000,  // row_b11 = addr b28
        0b01000000000000000000000000000,  // row_b10 = addr b27
        0b00100000000000000000000000000,  // row_b9 = addr b26
        0b00010000000000000000000000000,  // row_b8 = addr b25
        0b00001000000000000000000000000,  // row_b7 = addr b24
        0b00000100000000000000000000000,  // row_b6 = addr b23
        0b00000010000000000000000000000,  // row_b5 = addr b22
        0b00000001000000000000000000000,  // row_b4 = addr b21
        0b00000000100000000000000000000,  // row_b3 = addr b20
        0b00000000010000000000000000000,  // row_b2 = addr b19
        0b00000000001000000000000000000,  // row_b1 = addr b18
        0b00000000000100000000000000000,  // row_b0 = addr b17
        0b00000000000000001000000000000,  // col_b12 = addr b12
        0b00000000000000000100000000000,  // col_b11 = addr b11
        0b00000000000000000010000000000,  // col_b10 = addr b10
        0b00000000000000000001000000000,  // col_b9 = addr b9
        0b00000000000000000000100000000,  // col_b8 = addr b8
        0b00000000000000000000010000000,  // col_b7 = addr b7
        0b00000000000000000000001000000,  // col_b6 = addr b6
        0b00000000000000000000000100000,  // col_b5 = addr b5
        0b00000000000000000000000010000,  // col_b4 = addr b4
        0b00000000000000000000000001000,  // col_b3 = addr b3
        0b00000000000000000000000000100,  // col_b2 = addr b2
        0b00000000000000000000000000010,  // col_b1 = addr b1
        0b00000000000000000000000000001,  // col_b0 = addr b0
};

static const size_t zen2_addr_matrix[] = {
        0b00001000000000000000000000000,  // addr b28 = row_b11
        0b00000100000000000000000000000,  // addr b27 = row_b10
        0b00000010000000000000000000000,  // addr b26 = row_b9
        0b00000001000000000000000000000,  // addr b25 = row_b8
        0b00000000100000000000000000000,  // addr b24 = row_b7
        0b00000000010000000000000000000,  // addr b23 = row_b6
        0b00000000001000000000000000000,  // addr b22 = row_b5
        0b00000000000100000000000000000,  // addr b21 = row_b4
        0b00000000000010000000000000000,  // addr b20 = row_b3
        0b00000000000001000000000000000,  // addr b19 = row_b2
        0b00000000000000100000000000000,  // addr b18 = row_b1
        0b00000000000000010000000000000,  // addr b17 = row_b0
        0b10000010001000100000000000000,  // addr b16 = bg_b1 row_b9 row_b5 row_b1
        0b01000001000100010000000000000,  // addr b15 = bg_b0 row_b8 row_b4 row_b0
        0b00101000100010000000000000000,  // addr b14 = ba_b1 row_b11 row_b7 row_b3
        0b00010100010001001111111000000,  // addr b13 = ba_b0 row_b10 row_b6 row_b2 col_b12 col_b11 col_b10 col_b9 col_b8 col_b7 col_b6
        0b00000000000000001000000000000,  // addr b12 = col_b12
        0b00000000000000000100000000000,  // addr b11 = col_b11
        0b00000000000000000010000000000,  // addr b10 = col_b10
        0b00000000000000000001000000000,  // addr b9 = col_b9
        0b00000000000000000000100000000,  // addr b8 = col_b8
        0b00000000000000000000010000000,  // addr b7 = col_b7
        0b00000000000000000000001000000,  // addr b6 = col_b6
        0b00000000000000000000000100000,  // addr b5 = col_b5
        0b00000000000000000000000010000,  // addr b4 = col_b4
        0b00000000000000000000000001000,  // addr b3 = col_b3
        0b00000000000000000000000000100,  // addr b2 = col_b2
        0b00000000000000000000000000010,  // addr b1 = col_b1
        0b00000000000000000000000000001,  // addr b0 = col_b0
};

struct dram_config amd_zen2_config = {
    .name = "AMD Zen 2",
    .dram_matrix = zen2_dram_matrix,
    .addr_matrix = zen2_addr_matrix,

    .phys_dram_offset = 0x20000000,

    
    .bank_mask = 0b1111,  
    .bank_shift = 25,   

    
    .row_shift = 13,    
    .row_mask = 0b111111111111, 

    
    .column_mask = 0b1111111111111,  
    .column_shift = 0,    

    
    .matrix_size = 29,
};

EXPORT_SYMBOL(amd_zen2_config);




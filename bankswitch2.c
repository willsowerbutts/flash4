#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "bankswitch.h"

unsigned int default_mem_bank;
unsigned char bank_switch_method = 0xff;
unsigned int una_entry_vector = 0;
unsigned char bank_mask = 0x07;
unsigned int rom_bank_count = 0;

void init_bankswitch(unsigned char method)
{
    bank_switch_method = method;

    /* For UNA BIOS, we need to know where the RST 8 entry vector
     * takes us, since we're going to overlay this vector with
     * flash ROM contents and we need it to get our RAM back!    */
    if(bank_switch_method == BANKSWITCH_UNABIOS)
        una_entry_vector = *((unsigned int*)9);

    default_mem_bank = bankswitch_get_current_bank();
    rom_bank_count = bankswitch_get_rom_bank_count();
}

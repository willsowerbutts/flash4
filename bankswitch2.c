#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "bankswitch.h"

unsigned int default_mem_bank;
unsigned char bank_switch_method;

void init_bankswitch(unsigned char method)
{
    bank_switch_method = method;
    default_mem_bank = bankswitch_get_current_bank();
}

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "bankswitch.h"

unsigned char default_mem_bank;

void init_bankswitch(void)
{
    default_mem_bank = romwbw_sys_getbnk();
}

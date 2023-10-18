#ifndef __BANKSWITCH_DOT_H__
#define __BANKSWITCH_DOT_H__

#include "calling.h"

#define BANKSWITCH_ROMWBW_OLD   0 /* prior to v2.6 */
#define BANKSWITCH_UNABIOS      1
#define BANKSWITCH_P112         2
#define BANKSWITCH_ROMWBW_26    3 /* v2.6 and later */
#define BANKSWITCH_N8VEM_SBC    4

void init_bankswitch(unsigned char method);
unsigned int bankswitch_get_current_bank(void) CALLING;
unsigned int bankswitch_get_rom_bank_count(void) CALLING; /* only implemented for RomWBW 2.6+ */

void flashrom_chip_write_bankswitch(unsigned long address, unsigned char value) CALLING;
unsigned char flashrom_chip_read_bankswitch(unsigned long address) CALLING;
void flashrom_block_read_bankswitch(unsigned long address, unsigned char *buffer, unsigned int length) CALLING;
void flashrom_block_write_bankswitch(unsigned long address, unsigned char *buffer, unsigned int length) CALLING;
bool flashrom_block_verify_bankswitch(unsigned long address, unsigned char *buffer, unsigned int length) CALLING;

extern unsigned int default_mem_bank;
extern unsigned char bank_switch_method;
extern unsigned int rom_bank_count;

#endif

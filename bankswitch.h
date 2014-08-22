#ifndef __ROMWBW_DOT_H__
#define __ROMWBW_DOT_H__

#define BANKSWITCH_ROMWBW   0
#define BANKSWITCH_UNABIOS  1
#define BANKSWITCH_P112     2

void init_bankswitch(unsigned char method);
unsigned int bankswitch_get_current_bank(void);

void flashrom_chip_write_bankswitch(unsigned long address, unsigned char value);
unsigned char flashrom_chip_read_bankswitch(unsigned long address);
void flashrom_block_read_bankswitch(unsigned long address, unsigned char *buffer, unsigned int length);
void flashrom_block_write_bankswitch(unsigned long address, unsigned char *buffer, unsigned int length);
bool flashrom_block_verify_bankswitch(unsigned long address, unsigned char *buffer, unsigned int length);

extern unsigned int default_mem_bank;
extern unsigned char bank_switch_method;

#endif

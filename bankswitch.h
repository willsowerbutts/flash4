#ifndef __ROMWBW_DOT_H__
#define __ROMWBW_DOT_H__

unsigned char romwbw_sys_getbnk(void);
void romwbw_sys_setbnk(unsigned char bank);

void flashrom_chip_write_bankswitch(unsigned long address, unsigned char value);
unsigned char flashrom_chip_read_bankswitch(unsigned long address);
void flashrom_block_read_bankswitch(unsigned long address, unsigned char *buffer, unsigned int length);
void flashrom_block_write_bankswitch(unsigned long address, unsigned char *buffer, unsigned int length);

#endif

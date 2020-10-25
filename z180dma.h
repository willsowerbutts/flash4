#ifndef __Z180_DMA_DOT_H__
#define __Z180_DMA_DOT_H__

void dma_memory(unsigned long src, unsigned long dst, unsigned int length);

/* utility functions to read Z180 MMU registers */
unsigned char z180_cbr(void);
unsigned char z180_bbr(void);
unsigned char z180_cbar(void);

void init_z180dma(void);
void flashrom_chip_write_z180dma(unsigned long address, unsigned char value);
unsigned char flashrom_chip_read_z180dma(unsigned long address);
void flashrom_block_read_z180dma(unsigned long address, unsigned char *buffer, unsigned int length);
void flashrom_block_write_z180dma(unsigned long address, unsigned char *buffer, unsigned int length);
bool flashrom_block_verify_z180dma(unsigned long address, unsigned char *buffer, unsigned int length);

#endif

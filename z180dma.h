#ifndef __Z180_DMA_DOT_H__
#define __Z180_DMA_DOT_H__

#include "calling.h"

void dma_memory(unsigned long src, unsigned long dst, unsigned int length) CALLING;

/* utility functions to read Z180 MMU registers */
unsigned char z180_cbr(void) CALLING;
unsigned char z180_bbr(void) CALLING;
unsigned char z180_cbar(void) CALLING;

void init_z180dma(void);

/* note these are written in C but have to be compatible with the 
 * calling convention of the assembler versions */
void flashrom_chip_write_z180dma(unsigned long address, unsigned char value) CALLING;
unsigned char flashrom_chip_read_z180dma(unsigned long address) CALLING;
void flashrom_block_read_z180dma(unsigned long address, unsigned char *buffer, unsigned int length) CALLING;
void flashrom_block_write_z180dma(unsigned long address, unsigned char *buffer, unsigned int length) CALLING;
bool flashrom_block_verify_z180dma(unsigned long address, unsigned char *buffer, unsigned int length) CALLING;

#endif

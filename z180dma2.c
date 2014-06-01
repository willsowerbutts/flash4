#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "z180dma.h"

#define Z180_IO_BASE (0x40)
#include <z180/z180.h>

static unsigned char byte_buffer;        /* buffer for single byte to transfer to/from flash memory */
static unsigned long byte_buffer_paddr;  /* physical address of byte_buffer */

#define flashrom_to_physical(addr) (addr) /* translate flash address to physical memory address */

/* determine the physical address corresponding with a virtual address in the current MMU mapping */
unsigned long virtual_to_physical(void *_vaddr)
{
    unsigned int vaddr=(unsigned int)_vaddr;

    /* memory is arranged as (low->high) common0, banked, common1 */
    /* physical base of common0 is always 0. banked, common1 base specified by BBR, CBR */
    /* virtual start of banked, common1 specified by CBAR */

    if(vaddr < ((unsigned int)(CBAR & 0x0F)) << 12){ /* if address is before banked base, it's in common0 */
        return vaddr; /* unmapped */
    }

    if(vaddr < ((unsigned int)(CBAR & 0xF0)) << 8){ /* if before common1 base, it's in banked */
        return ((unsigned long)BBR << 12) + vaddr;
    }

    /* it's in common1 */
    return ((unsigned long)CBR << 12) + vaddr;
}

void flashrom_chip_write_z180dma(unsigned long address, unsigned char value)
{
    byte_buffer = value;
    dma_memory(byte_buffer_paddr, flashrom_to_physical(address), 1);
}

unsigned char flashrom_chip_read_z180dma(unsigned long address)
{
    dma_memory(flashrom_to_physical(address), byte_buffer_paddr, 1);
    return byte_buffer;
}

void flashrom_block_read_z180dma(unsigned long address, unsigned char *buffer, unsigned int length)
{
    dma_memory(flashrom_to_physical(address), virtual_to_physical(buffer), length);
}

void flashrom_program_byte_z180dma(unsigned long address, unsigned char value)
{
    unsigned char a, b;

    flashrom_chip_write_z180dma(0x5555, 0xAA);
    flashrom_chip_write_z180dma(0x2AAA, 0x55);
    flashrom_chip_write_z180dma(0x5555, 0xA0);
    flashrom_chip_write_z180dma(address, value);

    /* wait for toggle bit to indicate completion */
    do{
        a = flashrom_chip_read_z180dma(address);
        b = flashrom_chip_read_z180dma(address);
        if(a==b){
            /* data sheet says two additional reads are required */
            a = flashrom_chip_read_z180dma(address);
            b = flashrom_chip_read_z180dma(address);
        }
    }while(a != b);
}

void flashrom_block_write_z180dma(unsigned long address, unsigned char *buffer, unsigned int length)
{
    unsigned long offset = address;

    while(length--){
        flashrom_program_byte_z180dma(offset++, *(buffer++));
    }
}

void init_z180dma(void)
{
    /* Z180 DMA engine initialisation */
    byte_buffer_paddr = virtual_to_physical(&byte_buffer);
}

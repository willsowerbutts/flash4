#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "z180dma.h"
#include "buffers.h"
#include "calling.h"

static unsigned char byte_buffer;        /* buffer for single byte to transfer to/from flash memory */
static unsigned long byte_buffer_paddr;  /* physical address of byte_buffer */

#define flashrom_to_physical(addr) (addr) /* translate flash address to physical memory address */

/* determine the physical address corresponding with a virtual address in the current MMU mapping */
static unsigned long virtual_to_physical(void *_vaddr)
{
    unsigned int vaddr=(unsigned int)_vaddr;
    unsigned char cbar;

    /* memory is arranged as (low->high) common0, banked, common1 */
    /* physical base of common0 is always 0. banked, common1 base specified by BBR, CBR */
    /* virtual start of banked, common1 specified by CBAR */

    cbar = z180_cbar();
    if(vaddr < ((unsigned int)(cbar & 0x0F)) << 12){ /* if address is before banked base, it's in common0 */
        return vaddr; /* unmapped */
    }

    if(vaddr < ((unsigned int)(cbar & 0xF0)) << 8){ /* if before common1 base, it's in banked */
        return ((unsigned long)z180_bbr() << 12) + vaddr;
    }

    /* it's in common1 */
    return ((unsigned long)z180_cbr() << 12) + vaddr;
}

void flashrom_chip_write_z180dma(unsigned long address, unsigned char value) CALLING
{
    byte_buffer = value;
    dma_memory(byte_buffer_paddr, flashrom_to_physical(address), 1);
}

unsigned char flashrom_chip_read_z180dma(unsigned long address) CALLING
{
    dma_memory(flashrom_to_physical(address), byte_buffer_paddr, 1);
    return byte_buffer;
}

void flashrom_block_read_z180dma(unsigned long address, unsigned char *buffer, unsigned int length) CALLING
{
    dma_memory(flashrom_to_physical(address), virtual_to_physical(buffer), length);
}

bool flashrom_block_verify_z180dma(unsigned long address, unsigned char *buffer, unsigned int length) CALLING
{
    unsigned int bytes;

    while(length > 0){
        if(length >= CPM_BLOCK_SIZE)
            bytes = CPM_BLOCK_SIZE;
        else
            bytes = length;
        dma_memory(flashrom_to_physical(address), virtual_to_physical(rombuffer), bytes);
        if(memcmp(rombuffer, buffer, bytes))
            return false;

        address += bytes;
        buffer += bytes;
        length -= bytes;
    }

    return true;
}

void flashrom_program_byte_z180dma(unsigned long address, unsigned char value) CALLING
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

void flashrom_block_write_z180dma(unsigned long address, unsigned char *buffer, unsigned int length) CALLING
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

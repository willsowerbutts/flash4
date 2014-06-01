#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "libcpm.h"
#include "z180dma.h"
#include "bankswitch.h"
#include "detectcpu.h"

/*
 * TODO
 * - memory signature to detect UNABIOS? waiting on info from John.
 *   - make get/set bank vectors runtime switchable
 * - larger buffer size (4K?) to enable support for large sector atmel devices
 *   - when possible, avoid double read from disk on verify/write cycle
 *
 * 2014-05-29 Z180 DMA baseline time to reflash entire ROM plus verify from CF media is 1m49s (write random.img, then time write flash8.img)
 * 2014-05-31 Bank switched memory time same (compiler generated code) is 1m14s
 * 2014-05-31 Bank switched memory time same (hand assembly code) is 25s
 */

typedef struct {
    unsigned int chip_id;
    char *chip_name;
    unsigned int sector_size;  /* in multiples of 128 bytes */
    unsigned int sector_count;
    unsigned char strategy;
} flashrom_chip_t; 

/* the strategy byte describes how to program a given chip;
 *
 * bit 0:
 *   0 -- memory is programmed byte by byte (JEDEC style)
 *   1 -- memory is programmed sector by sector (Atmel style)
 * bit 7:
 *   1 -- chip is unsupported
 *   0 -- chip is supported
 */

#define ST_PROGRAM_SECTORS      (0x01) /* strategy byte, bit 0 */
#define ST_CHIP_NOT_SUPPORTED   (0x80) /* strategy byte, bit 7 */

static flashrom_chip_t flashrom_chips[] = {
    { 0x0120, "29F010",   128,    8, 0 },
    { 0x01A4, "29F040",   512,    8, 0 },
    { 0x1F5D, "AT29C512",   1,  512, ST_PROGRAM_SECTORS | ST_CHIP_NOT_SUPPORTED },
    { 0x1FA4, "AT29C040",   2, 2048, ST_PROGRAM_SECTORS | ST_CHIP_NOT_SUPPORTED },
    { 0x1FD5, "AT29C010",   1, 1024, ST_PROGRAM_SECTORS | ST_CHIP_NOT_SUPPORTED },
    { 0x1FDA, "AT29C020",   2, 1024, ST_PROGRAM_SECTORS | ST_CHIP_NOT_SUPPORTED },
    { 0x2020, "M29F010",  128,    8, 0 },
    { 0x20E2, "M29F040",  512,    8, 0 },
    { 0xBFB5, "39F010",    32,   32, 0 },
    { 0xBFB6, "39F020",    32,   64, 0 },
    { 0xBFB7, "39F040",    32,  128, 0 },
    { 0xC2A4, "MX29F040", 512,    8, 0 },
    /* terminate the list */
    { 0x0000, NULL,         0,    0, 0 }
};

/* storage for our buffers is defined in buffers.s so we can force them into the _BSS section */
extern unsigned char filebuffer[CPM_BLOCK_SIZE], rombuffer[CPM_BLOCK_SIZE];

static flashrom_chip_t *flashrom_type = NULL;
static unsigned long flashrom_size; /* bytes */
static unsigned long flashrom_sector_size; /* bytes */

/* this should really go into a bankswitch.c file */
unsigned char default_mem_bank;

void init_bankswitch(void)
{
    default_mem_bank = romwbw_sys_getbnk();
}

/* function pointers set at runtime to switch between bank switching and Z180 DMA engine */
void (*flashrom_chip_write)(unsigned long address, unsigned char value) = NULL;
unsigned char (*flashrom_chip_read)(unsigned long address) = NULL;
void (*flashrom_block_read)(unsigned long address, unsigned char *buffer, unsigned int length) = NULL;
void (*flashrom_block_write)(unsigned long address, unsigned char *buffer, unsigned int length) = NULL;

void abort_and_solicit_report(void)
{
    printf("Please email will@sowerbutts.com if you would like support for your\nsystem added to this program.\n");
    cpm_abort();
}

unsigned long flashrom_sector_address(unsigned int sector)
{
    return flashrom_sector_size * ((unsigned long)sector);
}

void flashrom_sector_erase(unsigned int sector)
{
    unsigned char a, b;
    unsigned long address;

    address = flashrom_sector_address(sector);

    flashrom_chip_write(0x5555, 0xAA);
    flashrom_chip_write(0x2AAA, 0x55);
    flashrom_chip_write(0x5555, 0x80);
    flashrom_chip_write(0x5555, 0xAA);
    flashrom_chip_write(0x2AAA, 0x55);
    flashrom_chip_write(address, 0x30);

    /* wait for toggle bit to indicate completion */
    do{
        a = flashrom_chip_read(address);
        b = flashrom_chip_read(address);
    }while(a != b);
}

bool flashrom_sector_verify(cpm_fcb *infile, unsigned int sector)
{
    unsigned long offset;
    unsigned int block, b, r;

    offset = flashrom_sector_address(sector);
    block = sector * flashrom_type->sector_size;

    for(b=0; b < flashrom_type->sector_size; b++){
        flashrom_block_read(offset, rombuffer, CPM_BLOCK_SIZE);
        r = cpm_f_read_random(infile, block, filebuffer);
        if(r){
            printf("cpm_f_read()=%d\n", r);
            cpm_abort();
        }
        if(memcmp(filebuffer, rombuffer, CPM_BLOCK_SIZE))
            return false;
        block++;
        offset += CPM_BLOCK_SIZE;
    }

    return true;
}

void flashrom_sector_program(cpm_fcb *infile, unsigned int sector)
{
    unsigned long offset;
    unsigned int block, b, r;

    offset = flashrom_sector_address(sector);
    block = sector * flashrom_type->sector_size;

    for(b=0; b < flashrom_type->sector_size; b++){
        r = cpm_f_read_random(infile, block, filebuffer);
        if(r){
            printf("cpm_f_read()=%d\n", r);
            cpm_abort();
        }
        flashrom_block_write(offset, filebuffer, CPM_BLOCK_SIZE);
        block++;
        offset += CPM_BLOCK_SIZE;
    }
}

bool flashrom_identify(void)
{
    unsigned int flashrom_device_id;

    /* put the flash memory into identify mode */
    flashrom_chip_write(0x5555, 0xAA);
    flashrom_chip_write(0x2AAA, 0x55);
    flashrom_chip_write(0x5555, 0x90);

    /* atmel chips require a pause for 10msec at this point; not implemented */

    /* load manufacturer and device IDs */
    flashrom_device_id = ((unsigned int)flashrom_chip_read(0x0000) << 8) | flashrom_chip_read(0x0001);

    /* put the flash memory back into read mode */
    flashrom_chip_write(0x5555, 0xF0);

    /* atmel chips require a pause for 10msec at this point; not implemented */

    printf("Flash memory chip ID is 0x%04X: ", flashrom_device_id);

    for(flashrom_type = flashrom_chips; flashrom_type->chip_id; flashrom_type++)
        if(flashrom_type->chip_id == flashrom_device_id)
            break;

    if(!flashrom_type->chip_id){
        /* we scanned the whole table without finding our chip */
        flashrom_type = NULL;
        flashrom_size = 0;
        flashrom_sector_size = 0;
        printf("Unknown flash chip.\n");
        return false;
    }

    printf("%s\n", flashrom_type->chip_name);

    flashrom_sector_size = (long)flashrom_type->sector_size * 128L;
    flashrom_size = flashrom_sector_size * (long)flashrom_type->sector_count;

    return true;
}

void flashrom_read(cpm_fcb *outfile)
{
    unsigned long offset;
    unsigned int block;
    int r;

    offset = 0;
    block = 0;

    while(offset < flashrom_size){
        if(!(offset & 0x3FF))
            printf("\rRead %d/%dKB ", (int)(offset >> 10), (int)(flashrom_size >> 10));
        flashrom_block_read(offset, rombuffer, CPM_BLOCK_SIZE);
        r = cpm_f_write_random(outfile, block++, rombuffer);
        if(r){
            printf("cpm_f_write()=%d\n", r);
            cpm_abort();
        }
        offset += CPM_BLOCK_SIZE;
    }

    printf("\rRead complete.\n");
}

void flashrom_write(cpm_fcb *infile)
{
    unsigned int sector;
    int reprogrammed = 0;

    if(flashrom_type->strategy & ST_PROGRAM_SECTORS){
        printf("Sector-wide programming is not yet supported.\n");
        abort_and_solicit_report();
    }

    for(sector=0; sector<flashrom_type->sector_count; sector++){
        printf("\rWrite: sector %d/%d ", sector, flashrom_type->sector_count);
        if(!flashrom_sector_verify(infile, sector)){
            flashrom_sector_erase(sector);
            flashrom_sector_program(infile, sector);
            reprogrammed++;
        }
    }

    printf("\rWrite complete: Reprogrammed %d/%d sectors.\n", reprogrammed, flashrom_type->sector_count);
}

bool flashrom_verify(cpm_fcb *infile)
{
    unsigned int sector;
    int errors = 0;

    for(sector=0; sector<flashrom_type->sector_count; sector++){
        printf("\rVerify: sector %d/%d ", sector, flashrom_type->sector_count);
        if(!flashrom_sector_verify(infile, sector)){
            errors++;
        }
    }

    printf("\rVerify complete: %d sectors contain errors.\n", errors);

    if(errors){
        printf("*** VERIFY FAILED ***\n");
        return false;
    }

    return true;
}

typedef enum { ACTION_UNKNOWN, ACTION_READ, ACTION_WRITE, ACTION_VERIFY } action_t;

typedef enum { ACCESS_NONE, ACCESS_AUTO, 
               ACCESS_ROMWBW, ACCESS_ROMWBW_DMA, 
               ACCESS_UNABIOS, ACCESS_UNABIOS_DMA, 
               ACCESS_Z180DMA } access_t;

access_t access_auto_select(void)
{
    unsigned int *romwbw_bios_signature = (unsigned int*)0x0040;
    bool z180_cpu;

    z180_cpu = detect_z180_cpu();

    if(*romwbw_bios_signature == 0xA857)
        return z180_cpu ? ACCESS_ROMWBW_DMA : ACCESS_ROMWBW;

    /*
    if(some_unabios_test)
        return z180_cpu ? ACCESS_UNABIOS_DMA : ACCESS_UNABIOS;
    */

    if(z180_cpu)
        return ACCESS_Z180DMA;

    return ACCESS_NONE;
}

void main(int argc, char *argv[])
{
    int i;
    cpm_fcb imagefile;
    action_t action = ACTION_UNKNOWN;
    access_t access = ACCESS_AUTO;

    printf("FLASH4 by Will Sowerbutts <will@sowerbutts.com> version 0.9.1\n\n");

    /* determine access mode */
    for(i=1; i<argc; i++){ /* check for manual mode override */
        if(strcmp(argv[i], "/Z180DMA") == 0)
            access = ACCESS_Z180DMA;
        else if(strcmp(argv[i], "/ROMWBW") == 0)
            access = ACCESS_ROMWBW;
        else if(strcmp(argv[i], "/UNABIOS") == 0)
            access = ACCESS_UNABIOS;
    }

    if(access == ACCESS_AUTO)
        access = access_auto_select();

    switch(access){
        case ACCESS_Z180DMA:
            printf("Using Z180 DMA engine.\n");
            /* fall through */
        case ACCESS_ROMWBW_DMA:
        case ACCESS_UNABIOS_DMA:
            init_z180dma();
            flashrom_chip_read = flashrom_chip_read_z180dma;
            flashrom_chip_write = flashrom_chip_write_z180dma;
            flashrom_block_read = flashrom_block_read_z180dma;
            flashrom_block_write = flashrom_block_write_z180dma;
            break;
        case ACCESS_NONE:
        case ACCESS_AUTO:
            printf("Cannot determine how to access your flash ROM chip.\n");
            abort_and_solicit_report();
    }

    switch(access){
        case ACCESS_UNABIOS:
        case ACCESS_UNABIOS_DMA:
            /* fiddle bank switching vectors for UNA BIOS */
            /* fall through */
        case ACCESS_ROMWBW:
        case ACCESS_ROMWBW_DMA:
            printf("Using %s bank switching", 
                    (access == ACCESS_ROMWBW || access == ACCESS_ROMWBW_DMA) ? "RomWBW" : "UNA BIOS");
            init_bankswitch();
            flashrom_chip_read = flashrom_chip_read_bankswitch;
            flashrom_chip_write = flashrom_chip_write_bankswitch;
            flashrom_block_write = flashrom_block_write_bankswitch;
            if(flashrom_block_read)
                printf(" and Z180 DMA engine");
            else
                flashrom_block_read = flashrom_block_read_bankswitch; /* this is slower so Z180 DMA is preferred */
            printf(".\n");
            break;
    }

    /* identify flash ROM chip */
    if(!flashrom_identify()){
        printf("Your flash memory chip is not recognised.\n");
        abort_and_solicit_report();
    }

    printf("Flash memory has %d sectors of %ld bytes, total %dKB\n", 
            flashrom_type->sector_count, flashrom_sector_size,
            flashrom_size >> 10);

    if(flashrom_type->strategy & ST_CHIP_NOT_SUPPORTED){
        printf("Your flash memory chip is not yet supported.\n");
        abort_and_solicit_report();
    }

    /* determine action */
    if(argc == 3){
        cpm_f_prepare(&imagefile, argv[2]);
        if(strcmp(argv[1], "READ") == 0)
            action = ACTION_READ;
        else if(strcmp(argv[1], "VERIFY") == 0)
            action = ACTION_VERIFY;
        else if(strcmp(argv[1], "WRITE") == 0)
            action = ACTION_WRITE;
    }

    if(action == ACTION_UNKNOWN){
        printf("\nSyntax:\n\tFLASH4 READ filename [options]\n" \
               "\tFLASH4 VERIFY filename [options]\n" \
               "\tFLASH4 WRITE filename [options]\n\n" \
               "Options (default is auto-detection including hybrid Z180 DMA/bank switching):\n" \
               "\t/Z180DMA\tForce only Z180 DMA engine\n" \
               "\t/ROMWBW\tForce only RomWBW bank switching\n" \
               "\t/UNABIOS\tForce only UNA BIOS bank switching\n");
        return;
    }

    /* execute action */
    switch(action){
        case ACTION_READ:
            cpm_f_delete(&imagefile);     /* remove existing file first */
            if(cpm_f_create(&imagefile)){
                printf("Cannot create file \"%s\".\n", argv[2]);
                return;
            }
            flashrom_read(&imagefile);
            break;
        case ACTION_VERIFY:
        case ACTION_WRITE:
            if(cpm_f_open(&imagefile)){
                printf("Cannot open file \"%s\".\n", argv[2]);
                return;
            }
            if(cpm_f_getsize(&imagefile) != (flashrom_type->sector_count * flashrom_type->sector_size)){
                printf("Image file size does not match ROM size: Aborting\n");
                return;
            }
            if(action == ACTION_WRITE)
                flashrom_write(&imagefile);
            flashrom_verify(&imagefile);
            break;
    }

    cpm_f_close(&imagefile);
}

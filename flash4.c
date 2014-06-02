#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "libcpm.h"
#include "z180dma.h"
#include "bankswitch.h"
#include "detectcpu.h"
#include "buffers.h"

/*
 * TODO
 * - memory signature to detect UNABIOS? waiting on info from John.
 *   - make get/set bank vectors runtime switchable
 *
 * 2014-05-29 Z180 DMA baseline time to reflash entire ROM plus verify from CF media is 1m49s (write random.img, then time write flash8.img)
 * 2014-05-31 Bank switched memory time same (compiler generated code) is 1m14s
 * 2014-05-31 Bank switched memory time same (hand assembly code) is 25s
 * 2014-06-01 Bank switched memory time same (hand assembly code, improved verify etc) is 21s
 */

typedef enum { ACTION_UNKNOWN, ACTION_READ, ACTION_WRITE, ACTION_VERIFY } action_t;
typedef enum { ACCESS_NONE, ACCESS_AUTO, ACCESS_ROMWBW, ACCESS_UNABIOS, ACCESS_Z180DMA } access_t;

typedef struct {
    unsigned int chip_id;
    char *chip_name;
    unsigned int sector_size;  /* in multiples of 128 bytes */
    unsigned int sector_count;
    unsigned char strategy;
} flashrom_chip_t; 

/* the strategy flags describe quirks for programming particular chips */
#define ST_NORMAL               (0x00) /* default -- no special strategy required */
#define ST_PROGRAM_SECTORS      (0x01) /* strategy byte, bit 0: program whole sectors (Atmel AT29C style) */
#define ST_ERASE_CHIP           (0x02) /* strategy byte, bit 1: erase whole chip (sector_count must be exactly 1) */

static flashrom_chip_t flashrom_chips[] = {
    { 0x0120, "29F010",      128,    8, ST_NORMAL },
    { 0x01A4, "29F040",      512,    8, ST_NORMAL },
    { 0x1F04, "AT49F001NT", 1024,    1, ST_ERASE_CHIP }, /* multiple but unequal sized sectors */
    { 0x1F05, "AT49F001N",  1024,    1, ST_ERASE_CHIP }, /* multiple but unequal sized sectors */
    { 0x1F07, "AT49F002N",  2048,    1, ST_ERASE_CHIP }, /* multiple but unequal sized sectors */
    { 0x1F08, "AT49F002NT", 2048,    1, ST_ERASE_CHIP }, /* multiple but unequal sized sectors */
    { 0x1F13, "AT49F040",   4096,    1, ST_ERASE_CHIP }, /* single sector device */
    { 0x1F5D, "AT29C512",      1,  512, ST_PROGRAM_SECTORS },
    { 0x1FA4, "AT29C040",      2, 2048, ST_PROGRAM_SECTORS },
    { 0x1FD5, "AT29C010",      1, 1024, ST_PROGRAM_SECTORS },
    { 0x1FDA, "AT29C020",      2, 1024, ST_PROGRAM_SECTORS },
    { 0x2020, "M29F010",     128,    8, ST_NORMAL },
    { 0x20E2, "M29F040",     512,    8, ST_NORMAL },
    { 0xBFB5, "39F010",       32,   32, ST_NORMAL },
    { 0xBFB6, "39F020",       32,   64, ST_NORMAL },
    { 0xBFB7, "39F040",       32,  128, ST_NORMAL },
    { 0xC2A4, "MX29F040",    512,    8, ST_NORMAL },
    /* terminate the list */
    { 0x0000, NULL,            0,    0, 0 }
};

static flashrom_chip_t *flashrom_type = NULL;
static unsigned long flashrom_size; /* bytes */
static unsigned long flashrom_sector_size; /* bytes */

/* function pointers set at runtime to switch between bank switching and Z180 DMA engine */
void (*flashrom_chip_write)(unsigned long address, unsigned char value) = NULL;
unsigned char (*flashrom_chip_read)(unsigned long address) = NULL;
void (*flashrom_block_read)(unsigned long address, unsigned char *buffer, unsigned int length) = NULL;
bool (*flashrom_block_verify)(unsigned long address, unsigned char *buffer, unsigned int length) = NULL;
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

void flashrom_wait_toggle_bit(unsigned long address)
{
    unsigned char a, b;

    /* wait for toggle bit to indicate completion */
    do{
        a = flashrom_chip_read(address);
        b = flashrom_chip_read(address);
        if(a==b){
            /* data sheet says two additional reads are required */
            a = flashrom_chip_read(address);
            b = flashrom_chip_read(address);
        }
    }while(a != b);
}

void flashrom_chip_erase(void)
{
    flashrom_chip_write(0x5555, 0xAA);
    flashrom_chip_write(0x2AAA, 0x55);
    flashrom_chip_write(0x5555, 0x80);
    flashrom_chip_write(0x5555, 0xAA);
    flashrom_chip_write(0x2AAA, 0x55);
    flashrom_chip_write(0x5555, 0x10);
    flashrom_wait_toggle_bit(0);
}

void flashrom_sector_erase(unsigned long address)
{
    flashrom_chip_write(0x5555, 0xAA);
    flashrom_chip_write(0x2AAA, 0x55);
    flashrom_chip_write(0x5555, 0x80);
    flashrom_chip_write(0x5555, 0xAA);
    flashrom_chip_write(0x2AAA, 0x55);
    flashrom_chip_write(address, 0x30);
    flashrom_wait_toggle_bit(address);
}

/* this is used only for programming atmel 29C parts which have a combined erase/program cycle */
void flashrom_sector_program(unsigned long address, unsigned char *buffer, unsigned int count)
{
    unsigned long prog_address;

    prog_address = address;

    flashrom_chip_write(0x5555, 0xAA);
    flashrom_chip_write(0x2AAA, 0x55);
    flashrom_chip_write(0x5555, 0xA0); /* software data protection activated */
    while(count--){
        flashrom_chip_write(prog_address++, *(buffer++));
    }

    flashrom_wait_toggle_bit(address);
}

void delay10ms(void)
{
    unsigned int a, b=0;
    /* delay for around 10msec (calibrated for ~40 MHz Z180, delay will be longer on slower CPUs) */
    for(a=0; a<18000; a++)
        b++;
}

bool flashrom_identify(void)
{
    unsigned int flashrom_device_id;

    /* put the flash memory into identify mode */
    flashrom_chip_write(0x5555, 0xAA);
    flashrom_chip_write(0x2AAA, 0x55);
    flashrom_chip_write(0x5555, 0x90);

    /* atmel 29C parts require a pause for 10msec at this point */
    delay10ms();

    /* load manufacturer and device IDs */
    flashrom_device_id = ((unsigned int)flashrom_chip_read(0x0000) << 8) | flashrom_chip_read(0x0001);

    /* put the flash memory back into read mode */
    flashrom_chip_write(0x5555, 0xF0);

    /* atmel 29C parts require a pause for 10msec at this point */
    delay10ms();

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

void read_data_from_file(cpm_fcb *infile, unsigned int block, unsigned int count)
{
    unsigned char *ptr;
    int r;

    ptr = filebuffer;

    while(count--){
        r = cpm_f_read_random(infile, block++, ptr);
        if(r){
            printf("cpm_f_read()=%d\n", r);
            cpm_abort();
        }
        ptr += CPM_BLOCK_SIZE;
    }
}

unsigned int flashrom_verify_and_write(cpm_fcb *infile, bool perform_write)
{
    unsigned int sector=0, block=0, subsector=0, mismatch=0;
    unsigned int subsectors_per_sector, blocks_per_subsector, bytes_per_subsector;
    unsigned long flash_address;
    bool verify_okay;

    /* We verify or program at most one sector at once. If a sector is larger
       than our memory buffer for data read from disk, we divide it up into
       multiple "subsectors". If a sector already contains the desired data we
       avoid reprogramming it (thanks to John Coffman for this super idea).    */

    subsectors_per_sector = flashrom_type->sector_size / FILEBUFFER_BLOCKS;
    if(subsectors_per_sector == 0){
        subsectors_per_sector = 1;
        blocks_per_subsector = flashrom_type->sector_size;
    }else{
        blocks_per_subsector = FILEBUFFER_BLOCKS;
        /* sanity check */
        if(flashrom_type->sector_size % blocks_per_subsector){
            printf("Unexpected sector size %d\n", flashrom_type->sector_size);
            abort_and_solicit_report();
        }
    }

    bytes_per_subsector = blocks_per_subsector * CPM_BLOCK_SIZE;

    if( ((flashrom_type->strategy & ST_ERASE_CHIP) && flashrom_type->sector_count != 1) ||
        ((flashrom_type->strategy & ST_PROGRAM_SECTORS) && subsectors_per_sector != 1)){
        printf("FAILED SANITY CHECKS :(\n");
        abort_and_solicit_report();
    }

    for(sector=0; sector < flashrom_type->sector_count; sector++){
        printf("\r%s: sector %d/%d ", perform_write ? "Write" : "Verify", sector, flashrom_type->sector_count);

        /* verify sector */
        flash_address = flashrom_sector_address(sector);
        block = sector * flashrom_type->sector_size;
        verify_okay = true;

        for(subsector=0; subsector < subsectors_per_sector; subsector++){
            read_data_from_file(infile, block, blocks_per_subsector);

            if(!flashrom_block_verify(flash_address, filebuffer, bytes_per_subsector)){
                verify_okay = false;
                break;
            }

            block += blocks_per_subsector;
            flash_address += bytes_per_subsector;
        }

        if(!verify_okay){
            mismatch++;
            if(perform_write){
                if(subsector){
                    /* we need to rewind to the first subsector */
                    flash_address = flashrom_sector_address(sector);
                    block = sector * flashrom_type->sector_size;
                    read_data_from_file(infile, block, blocks_per_subsector);
                    subsector = 0;
                }

                /* erase and program sector */
                if(flashrom_type->strategy & ST_PROGRAM_SECTORS){
                    /* This type of chip has a combined erase/program cycle that programs a whole
                       sector at once. The sectors are quite small (128 or 256 bytes) so there is
                       exactly 1 subsector (and we employ a sanity check to ensure this is true)   */
                    flashrom_sector_program(flash_address, filebuffer, bytes_per_subsector);
                }else{
                    if(flashrom_type->strategy & ST_ERASE_CHIP)
                        flashrom_chip_erase();
                    else
                        flashrom_sector_erase(flash_address);

                    while(true){
                        flashrom_block_write(flash_address, filebuffer, bytes_per_subsector);
                        subsector++;
                        if(subsector >= subsectors_per_sector)
                            break;
                        block += blocks_per_subsector;
                        flash_address += bytes_per_subsector;
                        read_data_from_file(infile, block, blocks_per_subsector);
                    }
                }
            }
        }
    }

    /* report outcome */
    if(perform_write){
        printf("\rWrite complete: Reprogrammed %d/%d sectors.\n", mismatch, flashrom_type->sector_count);
    }else{
        printf("\rVerify complete: %d/%d sectors contain errors.\n", mismatch, flashrom_type->sector_count);
        if(mismatch)
            printf("\n*** VERIFY FAILED ***\n\n");
    }

    return mismatch;
}

access_t access_auto_select(void)
{
    unsigned int *romwbw_signature = (unsigned int*)0x0040;      /* RomWBW places a 2-byte marker here */
    unsigned char *unabios_signature_a = (unsigned char*)0x0008; /* UNA uses RST8 for entry */
    unsigned char *unabios_signature_b = (unsigned char*)0xFFFD; /* UNA keeps an entry vector at top of RAM too */

    if(*romwbw_signature == 0xA857)
        return ACCESS_ROMWBW;

    if(unabios_signature_a[0] == 0xC3 && unabios_signature_a[1] == 0x80 && unabios_signature_a[2] == 0xFF &&
       unabios_signature_b[0] == 0xC3 && unabios_signature_b[1] == 0x80 && unabios_signature_b[2] == 0xFF)
        return ACCESS_UNABIOS;

    if(detect_z180_cpu())
        return ACCESS_Z180DMA;

    return ACCESS_NONE;
}

void main(int argc, char *argv[])
{
    int i;
    unsigned int mismatch;
    cpm_fcb imagefile;
    action_t action = ACTION_UNKNOWN;
    access_t access = ACCESS_AUTO;

    printf("FLASH4 by Will Sowerbutts <will@sowerbutts.com> version 1.0\n\n");

    /* determine access mode */
    for(i=1; i<argc; i++){ /* check for manual mode override */
        if(strcmp(argv[i], "/Z180DMA") == 0)
            access = ACCESS_Z180DMA;
        else if(strcmp(argv[i], "/ROMWBW") == 0)
            access = ACCESS_ROMWBW;
        else if(strcmp(argv[i], "/UNABIOS") == 0)
            access = ACCESS_UNABIOS;
        else if(argv[i][0] == '/'){
            printf("Unrecognised option \"%s\"\n", argv[i]);
            return;
        }
    }

    if(access == ACCESS_AUTO)
        access = access_auto_select();

    switch(access){
        case ACCESS_Z180DMA:
            printf("Using Z180 DMA engine.\n");
            init_z180dma();
            flashrom_chip_read    = flashrom_chip_read_z180dma;
            flashrom_chip_write   = flashrom_chip_write_z180dma;
            flashrom_block_read   = flashrom_block_read_z180dma;
            flashrom_block_write  = flashrom_block_write_z180dma;
            flashrom_block_verify = flashrom_block_verify_z180dma;
            break;
        case ACCESS_UNABIOS:
            /* fiddle bank switching vectors for UNA BIOS */
            printf("UNA BIOS is not supported at this time.\n");
            return;
            /* fall through */
        case ACCESS_ROMWBW:
            printf("Using %s bank switching.\n", (access == ACCESS_ROMWBW) ? "RomWBW" : "UNA BIOS");
            init_bankswitch();
            flashrom_chip_read    = flashrom_chip_read_bankswitch;
            flashrom_chip_write   = flashrom_chip_write_bankswitch;
            flashrom_block_read   = flashrom_block_read_bankswitch;
            flashrom_block_write  = flashrom_block_write_bankswitch;
            flashrom_block_verify = flashrom_block_verify_bankswitch;
            break;
        case ACCESS_NONE:
        case ACCESS_AUTO:
            printf("Cannot determine how to access your flash ROM chip.\n");
            abort_and_solicit_report();
    }

    /* identify flash ROM chip */
    if(!flashrom_identify()){
        printf("Your flash memory chip is not recognised.\n");
        abort_and_solicit_report();
    }

    printf("Flash memory has %d sectors of %ld bytes, total %dKB\n", 
            flashrom_type->sector_count, flashrom_sector_size,
            flashrom_size >> 10);

    /* determine action */
    if(argc >= 3){
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
               "Options (default is auto-detection)\n" \
               "\t/Z180DMA\tForce Z180 DMA engine\n" \
               "\t/ROMWBW \tForce RomWBW bank switching\n" \
               "\t/UNABIOS\tForce UNA BIOS bank switching\n");
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
                mismatch = flashrom_verify_and_write(&imagefile, true); /* we avoid verifying if nothing changed */
            else
                mismatch = 1; /* force a verify if we're not writing */
            if(mismatch)
                flashrom_verify_and_write(&imagefile, false);
            break;
    }

    cpm_f_close(&imagefile);
}

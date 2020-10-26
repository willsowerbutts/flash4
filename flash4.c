#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "libcpm.h"
#include "z180dma.h"
#include "bankswitch.h"
#include "detectcpu.h"
#include "buffers.h"

typedef enum { 
    ACTION_UNKNOWN, 
    ACTION_READ, 
    ACTION_WRITE, 
    ACTION_VERIFY 
} action_t;

static action_t action = ACTION_UNKNOWN;

typedef enum { 
    ACCESS_NONE, 
    ACCESS_AUTO,
    // through BIOS interfaces:
    ACCESS_ROMWBW_OLD, // prior to v2.6
    ACCESS_ROMWBW_26,  // v2.6 and later
    ACCESS_UNABIOS, 
    // direct hardware poking:
    ACCESS_Z180DMA,
    ACCESS_P112,
    ACCESS_N8VEM_SBC,
} access_t;

static access_t access = ACCESS_AUTO;

/* the strategy flags describe quirks for programming particular chips */
#define ST_NORMAL               (0x00) /* default: no special strategy required */
#define ST_PROGRAM_SECTORS      (0x01) /* bit 0: program sector (not byte) at a time (Atmel AT29C style) */
#define ST_ERASE_CHIP           (0x02) /* bit 1: erase whole chip (sector_count must be exactly 1) instead of individual sectors */

typedef struct {
    unsigned int chip_id;
    char *chip_name;
    unsigned int sector_size;  /* in multiples of 128 bytes */
    unsigned int sector_count;
    unsigned char strategy;
} flashrom_chip_t; 

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

/* special ROM entry for ROM/EPROM/EEPROM with /ROM switch */
static flashrom_chip_t rom_chip = { 0x0000, "rom", 8, 512, 0 }; /* 512 x 1KB "sectors" */
static flashrom_chip_t *flashrom_type = NULL;

static bool verbose = false;
static bool chip_count_forced = false;
static unsigned int chip_count = 1;        /* number of chips */
static unsigned long flashrom_chip_size;   /* individual chip size, in bytes */
static unsigned long flashrom_size;        /* total size of all chips, in bytes; always equal to chip_count * flashrom_chip_size */
static unsigned long flashrom_sector_size; /* chip sector size, in bytes */

/* function pointers set at runtime to switch between bank switching and Z180 DMA engine */
void (*flashrom_chip_write)(unsigned long address, unsigned char value) = NULL;
unsigned char (*flashrom_chip_read)(unsigned long address) = NULL;
void (*flashrom_block_read)(unsigned long address, unsigned char *buffer, unsigned int length) = NULL;
bool (*flashrom_block_verify)(unsigned long address, unsigned char *buffer, unsigned int length) = NULL;
void (*flashrom_block_write)(unsigned long address, unsigned char *buffer, unsigned int length) = NULL;

/* useful to provide some feedback that something is actually happening with large-sector devices */
#define SPINNER_LENGTH 4
static char spinner_char[SPINNER_LENGTH] = {'|', '/', '-', '\\'};
static unsigned char spinner_pos=0;

char spinner(void)
{
    spinner_pos = (spinner_pos + 1) % SPINNER_LENGTH;
    return spinner_char[spinner_pos];
}

void help(void)
{
    puts("\nSyntax:\n\tFLASH4 READ filename [options]\n" \
            "\tFLASH4 VERIFY filename [options]\n" \
            "\tFLASH4 WRITE filename [options]\n\n" \
            "Options (access method is auto-detected by default)\n" \
            "\t/V\t\tVerbose details about verify/program process\n" \
            "\t/PARTIAL\tAllow flashing a large ROM from a smaller image file\n" \
            "\t/ROM\t\tAllow read-only use of unknown chip types\n" \
            "\t/Z180DMA\tForce Z180 DMA engine\n" \
            "\t/UNABIOS\tForce UNA BIOS bank switching\n" \
            "\t/ROMWBW\t\tForce RomWBW (v2.6+) bank switching\n" \
            "\t/ROMWBWOLD\tForce RomWBW (v2.5 and earlier) bank switching\n" \
            "\t/P112\t\tForce P112 bank switching\n" \
            "\t/2 ... /9\tForce programming multiple devices");
    cpm_abort();
}

void abort_and_solicit_report(void)
{
    puts("Please email will@sowerbutts.com if you would like support for your\nsystem added to this program.");
    cpm_abort();
}

unsigned long flashrom_sector_address(unsigned int sector)
{
    return flashrom_sector_size * ((unsigned long)sector);
}

void flashrom_wait_toggle_bit(unsigned long address)
{
    unsigned char a, b, matches=0;

    /* wait for toggle bit to indicate completion */

    /* data sheet says two additional reads are required to match 
     * after the first match */
    do{
        a = flashrom_chip_read(address);
        b = flashrom_chip_read(address);
        if(a==b)
            matches++;
        else
            matches=0;
    }while(matches < 2);
}

unsigned long chip_base_address(unsigned long address)
{
    return address & ~(flashrom_chip_size-1);
}

void flashrom_chip_erase(unsigned long base_address)
{
    base_address = chip_base_address(base_address);
    flashrom_chip_write(base_address | 0x5555, 0xAA);
    flashrom_chip_write(base_address | 0x2AAA, 0x55);
    flashrom_chip_write(base_address | 0x5555, 0x80);
    flashrom_chip_write(base_address | 0x5555, 0xAA);
    flashrom_chip_write(base_address | 0x2AAA, 0x55);
    flashrom_chip_write(base_address | 0x5555, 0x10);
    flashrom_wait_toggle_bit(base_address);
}

void flashrom_sector_erase(unsigned long address)
{
    unsigned long base_address;
    base_address = chip_base_address(address);
    flashrom_chip_write(base_address | 0x5555, 0xAA);
    flashrom_chip_write(base_address | 0x2AAA, 0x55);
    flashrom_chip_write(base_address | 0x5555, 0x80);
    flashrom_chip_write(base_address | 0x5555, 0xAA);
    flashrom_chip_write(base_address | 0x2AAA, 0x55);
    flashrom_chip_write(address, 0x30);
    flashrom_wait_toggle_bit(address);
}

/* this is used only for programming atmel 29C parts which have a combined erase/program cycle */
void flashrom_sector_program(unsigned long address, unsigned char *buffer, unsigned int count)
{
    unsigned long prog_address;

    prog_address = chip_base_address(address);

    flashrom_chip_write(prog_address | 0x5555, 0xAA);
    flashrom_chip_write(prog_address | 0x2AAA, 0x55);
    flashrom_chip_write(prog_address | 0x5555, 0xA0); /* software data protection activated */

    prog_address = address;

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

unsigned int flashrom_identify_device(unsigned long base_address)
{
    unsigned int flashrom_device_id;

    /* put the flash memory into identify mode */
    flashrom_chip_write(base_address | 0x5555, 0xAA);
    flashrom_chip_write(base_address | 0x2AAA, 0x55);
    flashrom_chip_write(base_address | 0x5555, 0x90);

    /* atmel 29C parts require a pause for 10msec at this point */
    delay10ms();

    /* load manufacturer and device IDs */
    flashrom_device_id = ((unsigned int)flashrom_chip_read(base_address) << 8) | flashrom_chip_read(base_address | 0x0001);

    /* put the flash memory back into read mode */
    flashrom_chip_write(base_address | 0x5555, 0xF0);

    /* atmel 29C parts require a pause for 10msec at this point */
    delay10ms();

    return flashrom_device_id;
}

void flashrom_setup(void)
{
    if(flashrom_type){
        flashrom_sector_size = (unsigned long)flashrom_type->sector_size * 128L;
        flashrom_chip_size = flashrom_sector_size * (unsigned long)flashrom_type->sector_count;
    }else{
        /* reset parameters */
        flashrom_chip_size = 0;
        flashrom_sector_size = 0;
    }

    flashrom_size = flashrom_chip_size * (unsigned long)chip_count;
}

bool flashrom_identify(void)
{
    unsigned int flashrom_device_id;
    unsigned long address = 0;
    int chip;

    flashrom_device_id = flashrom_identify_device(address);

    printf("Flash memory chip ID is 0x%04X: ", flashrom_device_id);

    for(flashrom_type = flashrom_chips; flashrom_type->chip_id; flashrom_type++)
        if(flashrom_type->chip_id == flashrom_device_id)
            break;

    if(!flashrom_type->chip_id){
        /* we scanned the whole table without finding our chip */
        flashrom_type = NULL;
        puts("Unknown flash chip.");
        return false;
    }

    flashrom_setup();
    printf("%s (%dKB)\n", flashrom_type->chip_name, flashrom_type->sector_size * flashrom_type->sector_count / 8);

    /* RomWBW reports the number of 32KB ROM banks, allowing us to auto-detect 
       when multiple chips are installed. Do this only if the user has not
       manually specified multiple chips. rom_bank_count is zero if the BIOS
       cannot report the number of ROM banks. */
    if(rom_bank_count && !chip_count_forced){
        chip = rom_bank_count / (int)(flashrom_chip_size >> 15);
        if(chip > 1){
            printf("BIOS reports %d x 32KB ROM banks: %d chips\n", rom_bank_count, chip);
            chip_count = chip;
            flashrom_setup();
        }
    }

    /* check any additional chips are of the same type */
    for(chip=1; chip < chip_count; chip++){
        address += flashrom_chip_size;
        flashrom_device_id = flashrom_identify_device(address);
        if(verbose)
            printf("Chip at 0x%06lX has ID %04X\n", address, flashrom_device_id);
        if(flashrom_device_id != flashrom_type->chip_id){
            printf("Mismatched chip types: Flash chip %d has ID 0x%04X\n" \
                   "This program requires all flash chips to be of the same type.\n",
                   1+chip, flashrom_device_id);
            cpm_abort();
        }
    }

    return true;
}

void flashrom_read(cpm_fcb *outfile)
{
    unsigned long offset;
    unsigned int block;
    unsigned char r;

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

    puts("\rRead complete.");
}

bool read_data_from_file(cpm_fcb *infile, unsigned int block, unsigned int count)
{
    unsigned char *ptr, r;

    ptr = filebuffer;

    /* give the user something pretty to watch */
    if(!verbose){
        putchar('\x08');
        putchar(spinner());
    }

    while(count--){
        r = cpm_f_read_random(infile, block++, ptr);
        switch(r){
            case 1:
            case 4:
                return true;
            case 0:
                break;
            default:
                printf("cpm_f_read()=%d\n", r);
                cpm_abort();
        }

        ptr += CPM_BLOCK_SIZE;
    }

    return false; /* not EOF */
}

unsigned int flashrom_verify_and_write(cpm_fcb *infile, bool perform_write)
{
    unsigned int sector_count, sector=0, block=0, subsector=0, mismatch=0;
    unsigned int subsectors_per_sector, blocks_per_subsector, bytes_per_subsector;
    unsigned long flash_address;
    bool verify_okay;
    bool eof = false;

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
        puts("FAILED SANITY CHECKS :(");
        abort_and_solicit_report();
    }

    sector_count = chip_count * flashrom_type->sector_count;

    for(sector=0; (sector < sector_count) && !eof; sector++){
        printf("%s%s: sector %3d/%d %s", 
                verbose ? "" : "\r",
                perform_write ? "Write" : "Verify", 
                sector, sector_count,
                verbose ? "" : "  ");

        /* verify sector */
        flash_address = flashrom_sector_address(sector);
        block = sector * flashrom_type->sector_size;
        verify_okay = true;

        for(subsector=0; subsector < subsectors_per_sector; subsector++){
            if(read_data_from_file(infile, block, blocks_per_subsector)){
                eof = true;
                break;
            }else if(!flashrom_block_verify(flash_address, filebuffer, bytes_per_subsector)){
                verify_okay = false;
                break;
            }

            block += blocks_per_subsector;
            flash_address += bytes_per_subsector;
        }

        if(verbose)
            printf(verify_okay ? "verified\n" : "mismatch, ");

        if(!verify_okay){
            mismatch++;
            if(perform_write){
                if(subsector){
                    /* we need to rewind to the first subsector */
                    flash_address = flashrom_sector_address(sector);
                    block = sector * flashrom_type->sector_size;
                    eof = read_data_from_file(infile, block, blocks_per_subsector);
                    subsector = 0;
                }

                /* erase and program sector */
                if(flashrom_type->strategy & ST_PROGRAM_SECTORS){
                    /* This type of chip has a combined erase/program cycle that programs a whole
                       sector at once. The sectors are quite small (128 or 256 bytes) so there is
                       exactly 1 subsector (and we employ a sanity check to ensure this is true).
                       Additionally we can be sure that we are not at EOF yet. */
                    flashrom_sector_program(flash_address, filebuffer, bytes_per_subsector);
                }else{
                    if(flashrom_type->strategy & ST_ERASE_CHIP){
                        if(verbose)
                            printf("chip erase, ");
                        flashrom_chip_erase(flash_address);
                    }else{
                        if(verbose)
                            printf("sector erase, ");
                        flashrom_sector_erase(flash_address);
                    }

                    while(true){
                        flashrom_block_write(flash_address, filebuffer, bytes_per_subsector);
                        subsector++;
                        if(subsector >= subsectors_per_sector)
                            break;
                        block += blocks_per_subsector;
                        flash_address += bytes_per_subsector;
                        if(read_data_from_file(infile, block, blocks_per_subsector)){
                            eof = true;
                            break;
                        }
                    }

                    if(verbose)
                        puts("programmed");
                }
            }
        }

        /* P112 can address only first 32KB of any device */
        if(access == ACCESS_P112){
            if(sector >= ((32768 / 128) / flashrom_type->sector_size))
                eof = true; /* force EOF at end of addressable region */
        }
    }

    /* report outcome */
    if(perform_write){
        printf("\rWrite complete: Reprogrammed %d/%d sectors.\n", mismatch, sector_count);
    }else{
        if(sector != sector_count)
            printf("\rPartial verify (%d/%d sectors)", sector-1, sector_count);
        else
            printf("\rVerify (%d sectors)", sector_count);

        if(mismatch){
            printf(" complete: %d sectors contain errors.\n" \
                   "\n*** VERIFY FAILED ***\n\n", 
                   mismatch);
        }else
            puts(" complete: OK!");
    }

    return mismatch;
}

bool check_file_size(cpm_fcb *imagefile, bool allow_partial)
{
    unsigned int file_size;
    unsigned int rom_size;

    file_size = cpm_f_getsize(imagefile);
    rom_size = flashrom_type->sector_count * flashrom_type->sector_size * chip_count;

    if(file_size == rom_size)
        return true;

    if(allow_partial &&
       (rom_size > file_size) && 
       (file_size != 0) &&
       (file_size & 0xff) == 0) /* file is exact multiple of 32KB long */
        return true;

    return false;
}

bool una_bios_present(void)
{
    unsigned int **bios_signature = (unsigned int **)BIOS_SIGNATURE_ADDR;
    return (**bios_signature == BIOS_SIGNATURE_UNA);
}

bool romwbw_bios_present(void)
{
    unsigned int **bios_signature = (unsigned int **)BIOS_SIGNATURE_ADDR;
    return (**bios_signature == BIOS_SIGNATURE_ROMWBW_26);
}

bool old_romwbw_bios_present(void)
{
    return (*((unsigned int*)CPM_SIGNATURE_ADDR) == CPM_SIGNATURE_ROMWBW);
}

static const char *bpbios_p112_signature = "B/P-DX";
bool bpbios_p112_present(void)
{
    return (memcmp((const char*)(*((unsigned int*)BIOS_ENTRY_ADDR) + 0x75), bpbios_p112_signature, 6) == 0);
}

access_t access_auto_select(void)
{
    // Note that versions of RomWBW before approx 2014-08 place a
    // signature at CPM_SIGNATURE_ADDR but not BIOS_SIGNATURE_ADDR.
    // Therefore we cannot rely on the latter to confirm if RomWBW
    // HBIOS is present or not.
    if(una_bios_present())
        return ACCESS_UNABIOS;

    if(bpbios_p112_present())
        return ACCESS_P112;

    if(romwbw_bios_present()) // important to check this before old_romwbw_bios_present()
        return ACCESS_ROMWBW_26;

    if(old_romwbw_bios_present())
        return ACCESS_ROMWBW_OLD;

    if(detect_z180_cpu())
        return ACCESS_Z180DMA;

    return ACCESS_NONE;
}

void main(int argc, const char *argv[])
{
    int i;
    unsigned int mismatch;
    cpm_fcb imagefile;
    const char *filename = NULL;
    bool allow_partial=false;
    bool rom_mode=false;

    puts("FLASH4 by Will Sowerbutts <will@sowerbutts.com> version 1.3.4\n");

    /* determine access mode */
    for(i=1; i<argc; i++){ /* check for manual mode override */
        if(strcmp(argv[i], "/Z180DMA") == 0)
            access = ACCESS_Z180DMA;
        else if(strcmp(argv[i], "/ROMWBWOLD") == 0)
            access = ACCESS_ROMWBW_OLD;
        else if(strcmp(argv[i], "/ROMWBW") == 0)
            access = ACCESS_ROMWBW_26;
        else if(strcmp(argv[i], "/UNABIOS") == 0)
            access = ACCESS_UNABIOS;
        else if(strcmp(argv[i], "/P112") == 0)
            access = ACCESS_P112;
        else if(strcmp(argv[i], "/N8VEMSBC") == 0)
            access = ACCESS_N8VEM_SBC;
        else if(strcmp(argv[i], "/ROM") == 0)
            rom_mode = true;
        else if(strcmp(argv[i], "/V") == 0)
            verbose = true;
        else if(strcmp(argv[i], "/P") == 0 || strcmp(argv[i], "/PARTIAL") == 0)
            allow_partial = true;
        else if(argv[i][0] == '/' && argv[i][1] >= '1' && argv[i][1] <= '9'){
            chip_count = argv[i][1] - '0';
            chip_count_forced = true;
        }else if(argv[i][0] == '/'){
            printf("Unrecognised option \"%s\"\n", argv[i]);
            help();
        }else{
            /* non-option command line parameters */
            if(action == ACTION_UNKNOWN){ /* action comes first */
                if(strcmp(argv[i], "READ") == 0)
                    action = ACTION_READ;
                else if(strcmp(argv[i], "VERIFY") == 0)
                    action = ACTION_VERIFY;
                else if(strcmp(argv[i], "WRITE") == 0)
                    action = ACTION_WRITE;
                else{
                    printf("Unrecognised command \"%s\"\n", argv[i]);
                    help();
                }
            }else if(filename == NULL){
                filename = argv[i];
            }else{
                printf("Unexpected command line argument \"%s\"\n", argv[i]);
                help();
            }
        }
    }

    if(access == ACCESS_AUTO)
        access = access_auto_select();

    // assume bank switching
    flashrom_chip_read    = flashrom_chip_read_bankswitch;
    flashrom_chip_write   = flashrom_chip_write_bankswitch;
    flashrom_block_read   = flashrom_block_read_bankswitch;
    flashrom_block_write  = flashrom_block_write_bankswitch;
    flashrom_block_verify = flashrom_block_verify_bankswitch;

    switch(access){
        case ACCESS_Z180DMA:
            puts("Using Z180 DMA engine.");
            if(chip_count != 1){
                puts("Z180 DMA engine supports programming a single device only.");
                return;
            }
            init_z180dma();
            flashrom_chip_read    = flashrom_chip_read_z180dma;
            flashrom_chip_write   = flashrom_chip_write_z180dma;
            flashrom_block_read   = flashrom_block_read_z180dma;
            flashrom_block_write  = flashrom_block_write_z180dma;
            flashrom_block_verify = flashrom_block_verify_z180dma;
            break;
        case ACCESS_UNABIOS:
            puts("Using UNA BIOS bank switching.");
            init_bankswitch(BANKSWITCH_UNABIOS);
            break;
        case ACCESS_ROMWBW_OLD:
            puts("Using RomWBW (old) bank switching.");
            init_bankswitch(BANKSWITCH_ROMWBW_OLD);
            break;
        case ACCESS_ROMWBW_26:
            puts("Using RomWBW (v2.6+) bank switching.");
            init_bankswitch(BANKSWITCH_ROMWBW_26);
            break;
        case ACCESS_P112:
            puts("Using P112 bank switching.");
            init_bankswitch(BANKSWITCH_P112);
            break;
        case ACCESS_N8VEM_SBC:
            puts("Using N8VEM SBC bank switching.");
            init_bankswitch(BANKSWITCH_N8VEM_SBC);
            break;
        case ACCESS_NONE:
        case ACCESS_AUTO:
            puts("Cannot determine how to access your flash ROM chip.");
            abort_and_solicit_report();
    }

    /* identify flash ROM chip */
    if(!flashrom_identify()){
        puts("Your flash memory chip is not recognised.");
        if(rom_mode && (action == ACTION_VERIFY || action == ACTION_READ)){
            puts("Assuming 512KB ROM");
            flashrom_type = &rom_chip;
            flashrom_setup();
        }else{
            abort_and_solicit_report();
        }
    }

    printf("Flash memory has %d chip%s %d sectors of %ld bytes, total %dKB\n",
            chip_count, chip_count == 1 ? ",":"s, each", 
            flashrom_type->sector_count, flashrom_sector_size,
            (int)(flashrom_size >> 10));

    /* P112 with ROMs larger than 32KB are limited */
    if(access == ACCESS_P112 && flashrom_size > 32768){
        puts("P112 can address only first 32KB: Partial mode enabled.");
        allow_partial = true;
        chip_count = 1;
        flashrom_chip_size = 32768;
        flashrom_size = 32768;
    }

    if(action == ACTION_UNKNOWN || !filename)
        help();

    cpm_f_prepare(&imagefile, filename);

    /* execute action */
    switch(action){
        case ACTION_READ:
            cpm_f_delete(&imagefile);     /* remove existing file first */
            if(cpm_f_create(&imagefile)){
                printf("Cannot create file \"%s\".\n", filename);
                return;
            }
            flashrom_read(&imagefile);
            break;
        case ACTION_VERIFY:
        case ACTION_WRITE:
            if(cpm_f_open(&imagefile)){
                printf("Cannot open file \"%s\".\n", filename);
                return;
            }
            if(!check_file_size(&imagefile, allow_partial)){
                puts("Image file size does not match ROM size: Aborting\n" \
                     "You may use /PARTIAL to program only the start of the ROM, however for\n" \
                     "safety reasons the image file must be a multiple of exactly 32KB long.");
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


/* 
    FLASH030: in-system flash ROM programmer for Linux on KISS-68030, based on FLASH4.
    (c) Will Sowerbutts <will@sowerbutts.com> 2016-02-19
    GPL Licensed 

    Compile with: gcc -O2 -Wall flash030.c -o flash030
*/

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define FLASHROM_PHYSICAL_BASE   0xFFF00000  /* Location in the physical address space */
#define FLASHROM_PHYSICAL_LENGTH (512*1024)  /* Size (in bytes) */

typedef enum { 
    ACTION_UNKNOWN, 
    ACTION_READ, 
    ACTION_WRITE, 
    ACTION_VERIFY 
} action_t;

static action_t action = ACTION_UNKNOWN;
bool allow_partial=false;
int mem_fd;
unsigned char volatile *flashrom_mapping;

typedef struct {
    unsigned int chip_id;
    char *chip_name;
    unsigned int sector_size;  /* in multiples of 128 bytes */
    unsigned int sector_count;
    unsigned char strategy;
} flashrom_chip_t; 

/* the strategy flags describe quirks for programming particular chips */
#define ST_NORMAL               (0x00) /* default: no special strategy required */
#define ST_PROGRAM_SECTORS      (0x01) /* bit 0: program sector (not byte) at a time (Atmel AT29C style) */
#define ST_ERASE_CHIP           (0x02) /* bit 1: erase whole chip (sector_count must be exactly 1) instead of individual sectors */

static flashrom_chip_t flashrom_chips[] = {
    { 0x0120, "29F010",        16384,    8, ST_NORMAL },
    { 0x01A4, "29F040",        65536,    8, ST_NORMAL },
    { 0x1F04, "AT49F001NT",   131072,    1, ST_ERASE_CHIP }, /* multiple but unequal sized sectors */
    { 0x1F05, "AT49F001N",    131072,    1, ST_ERASE_CHIP }, /* multiple but unequal sized sectors */
    { 0x1F07, "AT49F002N",    262144,    1, ST_ERASE_CHIP }, /* multiple but unequal sized sectors */
    { 0x1F08, "AT49F002NT",   262144,    1, ST_ERASE_CHIP }, /* multiple but unequal sized sectors */
    { 0x1F13, "AT49F040",     524288,    1, ST_ERASE_CHIP }, /* single sector device */
    { 0x1F5D, "AT29C512",        128,  512, ST_PROGRAM_SECTORS },
    { 0x1FA4, "AT29C040",        256, 2048, ST_PROGRAM_SECTORS },
    { 0x1FD5, "AT29C010",        128, 1024, ST_PROGRAM_SECTORS },
    { 0x1FDA, "AT29C020",        256, 1024, ST_PROGRAM_SECTORS },
    { 0x2020, "M29F010",       16384,    8, ST_NORMAL },
    { 0x20E2, "M29F040",       65536,    8, ST_NORMAL },
    { 0xBFB5, "39F010",         4096,   32, ST_NORMAL },
    { 0xBFB6, "39F020",         4096,   64, ST_NORMAL },
    { 0xBFB7, "39F040",         4096,  128, ST_NORMAL },     /* recommended device */
    { 0xC2A4, "MX29F040",      65536,    8, ST_NORMAL },
    /* terminate the list */
    { 0x0000, NULL,            0,    0, 0 }
};

static flashrom_chip_t *flashrom_type = NULL;
static unsigned int flashrom_size; /* bytes */

unsigned char flashrom_chip_read(unsigned long address)
{
    return flashrom_mapping[address];
}

void flashrom_chip_write(unsigned long address, unsigned char value)
{
    flashrom_mapping[address] = value;
}

void abort_and_solicit_report(void)
{
    printf("Please email will@sowerbutts.com if you would like support for your\nsystem added to this program.\n");
    _exit(1);
}

unsigned long flashrom_sector_address(unsigned int sector)
{
    return flashrom_type->sector_size * ((unsigned long)sector);
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

void flashrom_block_write(unsigned long address, const unsigned char *buffer, unsigned int length)
{
    unsigned char volatile *flashrom_ptr;
    
    flashrom_ptr = &flashrom_mapping[address];
    while(length--){
        if(*buffer != 0xFF){
            // enter programming mode
            flashrom_chip_write(0x5555, 0xAA);
            flashrom_chip_write(0x2AAA, 0x55);
            flashrom_chip_write(0x5555, 0xA0);

            *flashrom_ptr = *buffer;

            // data sheet advises you do this twice
            while(*flashrom_ptr != *buffer);
            while(*flashrom_ptr != *buffer);
        }
        buffer++;
        flashrom_ptr++;
    }
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
void flashrom_sector_program(unsigned long address, const unsigned char *buffer, unsigned int count)
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

bool flashrom_identify(void)
{
    unsigned int flashrom_device_id;

    /* put the flash memory into identify mode */
    flashrom_chip_write(0x5555, 0xAA);
    flashrom_chip_write(0x2AAA, 0x55);
    flashrom_chip_write(0x5555, 0x90);

    /* atmel 29C parts require a pause for 10msec at this point */
    usleep(10000);

    /* load manufacturer and device IDs */
    flashrom_device_id = (((unsigned int)flashrom_chip_read(0x0000) & 0xFF) << 8) 
                         | (flashrom_chip_read(0x0001) & 0xFF);

    /* put the flash memory back into normal mode */
    flashrom_chip_write(0x5555, 0xF0);

    /* atmel 29C parts require a pause for 10msec at this point */
    usleep(10000);

    printf("Flash memory chip ID is 0x%04X: ", flashrom_device_id);

    for(flashrom_type = flashrom_chips; flashrom_type->chip_id; flashrom_type++)
        if(flashrom_type->chip_id == flashrom_device_id)
            break;

    if(!flashrom_type->chip_id){
        /* we scanned the whole table without finding our chip */
        flashrom_type = NULL;
        printf("Unknown flash chip.\n");
        return false;
    }else{
        printf("%s\n", flashrom_type->chip_name);
        return true;
    }
}

#define READ_CHUNK_SIZE 4096
void flashrom_read(int img_fd)
{
    unsigned long offset;
    ssize_t w;

    offset = 0;
    while(offset < flashrom_size){
        printf("\rRead %d/%dKB ", (int)(offset >> 10), (int)(flashrom_size >> 10));
        fflush(stdout);
        w = write(img_fd, (char*)&flashrom_mapping[offset], READ_CHUNK_SIZE);
        if(w != READ_CHUNK_SIZE){
            printf("write() failed: %s\n", strerror(errno));
            _exit(1);
        }
        offset += READ_CHUNK_SIZE;
    }

    printf("\rRead complete.       \n");
}

unsigned int flashrom_verify_and_write(const unsigned char *rom_image, bool perform_write)
{
    unsigned int sector=0, mismatch=0;
    unsigned int offset;
    bool eof = false;

    /* We verify or program at most one sector at once. If a sector already
     * contains the desired data we avoid reprogramming it (thanks to John
     * Coffman for this super idea). */

    for(sector=0; (sector < flashrom_type->sector_count) && !eof; sector++){
        printf("\r%s: sector %d/%d   ", perform_write ? "Write" : "Verify", sector, flashrom_type->sector_count);
        fflush(stdout);

        /* verify sector */
        offset = flashrom_sector_address(sector);

        if(memcmp(&rom_image[offset], (char*)&flashrom_mapping[offset], flashrom_type->sector_size)){
            mismatch++;
            if(perform_write){
                /* erase and program sector */
                if(flashrom_type->strategy & ST_PROGRAM_SECTORS){
                    /* This type of chip has a combined erase/program cycle that programs a whole
                       sector at once. The sectors are quite small (128 or 256 bytes). */
                    flashrom_sector_program(offset, &rom_image[offset], flashrom_type->sector_size);
                }else{
                    if(flashrom_type->strategy & ST_ERASE_CHIP)
                        flashrom_chip_erase();
                    else
                        flashrom_sector_erase(offset);

                    flashrom_block_write(offset, &rom_image[offset], flashrom_type->sector_size);
                }
            }
        }
    }

    /* report outcome */
    if(perform_write){
        printf("\rWrite complete: Reprogrammed %d/%d sectors.\n", mismatch, flashrom_type->sector_count);
    }else{
        if(sector != flashrom_type->sector_count)
            printf("\rPartial verify (%d/%d sectors)", sector-1, flashrom_type->sector_count);
        else
            printf("\rVerify (%d sectors)", flashrom_type->sector_count);

        if(mismatch){
            printf(" complete: %d sectors contain errors.\n", mismatch);
            printf("\n*** VERIFY FAILED ***\n\n");
        }else
            printf(" complete: OK!\n");
    }

    return mismatch;
}

bool check_file_size(unsigned int file_size)
{
    if(file_size == flashrom_size)
        return true;

    if(file_size > flashrom_size)
        return false;

    if(allow_partial &&
       (flashrom_size > file_size) && 
       (file_size != 0))
        return true;

    return false;
}

bool map_flashrom(void)
{
    mem_fd = open("/dev/mem", O_RDWR | O_SYNC);

    if(mem_fd < 0){
        printf("Cannot open /dev/mem: %s\n", strerror(errno));
        return false;
    }

    flashrom_mapping = mmap(NULL, FLASHROM_PHYSICAL_LENGTH, 
            PROT_READ|PROT_WRITE, MAP_SHARED, 
            mem_fd, FLASHROM_PHYSICAL_BASE);

    if(flashrom_mapping == MAP_FAILED){
        printf("Cannot map from /dev/mem: %s\n", strerror(errno));
        return false;
    }

    return true;
}

void unmap_flashrom(void)
{
    munmap((void*)flashrom_mapping, FLASHROM_PHYSICAL_LENGTH);
    close(mem_fd);
}

unsigned char *read_rom_image(int img_fd)
{
    unsigned char *img_data;
    ssize_t r;
    unsigned int img_size, offset;

    img_size = lseek(img_fd, 0, SEEK_END);
    if(!check_file_size(img_size)){
        printf("Image file size does not match ROM size: Aborting\n" \
               "You may use '--partial' to program only the part of the ROM\n" \
               "from this file.\n");
        return NULL;
    }

    img_data=(unsigned char*)malloc(flashrom_size);
    if(!img_data){
        printf("Out of memory!\n");
        return NULL;
    }

    lseek(img_fd, 0, SEEK_SET);
    for(offset = 0; offset < img_size;){
        r = read(img_fd, &img_data[offset], img_size - offset);
        if(r < 0){
            printf("read() failed: %s\n", strerror(errno));
            free(img_data);
            return NULL;
        }
        offset += r;
    }

    if(img_size < flashrom_size) /* pad with unprogrammed bytes if space left over */
        memset(&img_data[img_size], 0xFF, flashrom_size - img_size);

    return img_data;
}

void usage(const char *cmdname)
{
    printf("Usage: %s [OPTION...] COMMAND filename\n", cmdname);
    printf("\nOPTION:\n");
    printf(" -h --help      This usage summary\n");
    printf(" -p --partial   Allow ROM and file sizes to differ\n");
    printf("\nCOMMAND:\n");
    printf(" -r --read      Read ROM conents out to file\n");
    printf(" -v --verify    Compare ROM contents to file\n");
    printf(" -w --write     Rewrite ROM contents from file\n");
}

int main(int argc, const char *argv[])
{
    int i, img_fd = -1;
    unsigned int mismatch;
    unsigned char *img_data;
    const char *filename = NULL;
    printf("FLASH030 by Will Sowerbutts <will@sowerbutts.com> version 1.0.0\n\n");

    // command line arguments
    for(i=1; i<argc; i++){
        if(strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--partial") == 0){
            allow_partial = true;
        }else if(strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0){
            usage(argv[0]);
            return 0;
        }else if(strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--read") == 0){
            if(action != ACTION_UNKNOWN){
                printf("More than one command specified!\n");
                usage(argv[0]);
                return 1;
            }
            action = ACTION_READ;
        }else if(strcmp(argv[i], "-w") == 0 || strcmp(argv[i], "--write") == 0){
            if(action != ACTION_UNKNOWN){
                printf("More than one command specified!\n");
                usage(argv[0]);
                return 1;
            }
            action = ACTION_WRITE;
        }else if(strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verify") == 0){
            if(action != ACTION_UNKNOWN){
                printf("More than one command specified!\n");
                usage(argv[0]);
                return 1;
            }
            action = ACTION_VERIFY;
        }else{
            if(filename == NULL)
                filename = argv[i];
            else{
                printf("Unrecognised option \"%s\"\n", argv[i]);
                usage(argv[0]);
                return 1;
            }
        }
    }
    
    if(action == ACTION_UNKNOWN){
        printf("No command specified!\n");
        usage(argv[0]);
        return 1;
    }

    if(!map_flashrom())
        return 1;

    /* identify flash ROM chip */
    if(!flashrom_identify()){
        printf("Your flash memory chip is not recognised.\n");
        abort_and_solicit_report();
    }

    flashrom_size = flashrom_type->sector_size * (long)flashrom_type->sector_count;

    printf("Flash memory has %d sectors of %d bytes, total %dKB\n", 
            flashrom_type->sector_count, flashrom_type->sector_size,
            flashrom_size >> 10);

    /* execute action */
    switch(action){
        case ACTION_READ:
            img_fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
            if(img_fd < 0){
                printf("Cannot create image file \"%s\": %s\n", filename, strerror(errno));
                return 1;
            }
            flashrom_read(img_fd);
            break;
        case ACTION_VERIFY:
        case ACTION_WRITE:
            img_fd = open(filename, O_RDONLY);
            if(img_fd < 0){
                printf("Cannot open image file \"%s\": %s\n", filename, strerror(errno));
                return 1;
            }
            img_data = read_rom_image(img_fd);
            if(!img_data)
                return 1;
            if(action == ACTION_VERIFY)
                mismatch = 1;
            else /* ACTION_WRITE */
                mismatch = flashrom_verify_and_write(img_data, true); /* we avoid verifying if nothing changed */
            if(mismatch)
                flashrom_verify_and_write(img_data, false);
            free(img_data);
            break;
        default:
            printf("?!?!\n");
            _exit(1);
    }

    unmap_flashrom();

    if(img_fd >= 0)
        close(img_fd);

    return 0;
}


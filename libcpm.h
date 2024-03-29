#ifndef __LIBCPM_DOT_H__
#define __LIBCPM_DOT_H__

#include "calling.h"
#define CPM_BLOCK_SIZE 128

// addresses of interesting data in the zero page
#define BIOS_ENTRY_ADDR               0x0001
#define BDOS_ENTRY_ADDR               0x0005

#define CPM_SIGNATURE_ADDR            0x0040
#define CPM_SIGNATURE_ROMWBW          0xA857
#define CPM_SIGNATURE_UNACPM          0x05B1

#define BIOS_SIGNATURE_ADDR           0xFFFE
#define BIOS_SIGNATURE_UNA            0xE5FD
#define BIOS_SIGNATURE_ROMWBW_26      0xA857 // present in v2.6 and later

/* File control block -- see http://www.seasip.demon.co.uk/Cpm/fcb.html */
typedef struct cpm_fcb {
    unsigned char dr;
    char name[8];
    char ext[3];
    unsigned char ex;       /* current extent number */
    unsigned char s1;
    unsigned char s2;
    unsigned char rc;       /* record count for extent 'ex' */
    unsigned char al[16];
    unsigned char cr;       /* current record */
    unsigned char r0;       /* random access record number (low byte) */
    unsigned char r1;       /* random access record number (middle byte) */
    unsigned char r2;       /* random access record number (high byte) */
} cpm_fcb;

void cpm_abort(void) CALLING;
void cpm_f_prepare(cpm_fcb *fcb, const char *name);                 /* (note: C) set filename in FCB, etc */
int cpm_f_delete(cpm_fcb *fcb) CALLING;                       /* delete a file */
int cpm_f_open(cpm_fcb *fcb) CALLING;                         /* open a file */
int cpm_f_create(cpm_fcb *fcb) CALLING;                       /* create a file */
int cpm_f_close(cpm_fcb *fcb) CALLING;                        /* close a file */
unsigned int cpm_f_getsize(cpm_fcb *fcb) CALLING;             /* return file size, in 128-byte blocks */

/* sequential block I/O */
unsigned char cpm_f_read_next(cpm_fcb *fcb, char *buffer) CALLING;      /* read the next 128-byte block from file */
unsigned char cpm_f_write_next(cpm_fcb *fcb, char *buffer) CALLING;     /* write the next 128-byte block to file */

/* random block I/O */
unsigned char cpm_f_read_random(cpm_fcb *fcb, unsigned int block, char *buffer) CALLING;    /* read 128-byte block from file */
unsigned char cpm_f_write_random(cpm_fcb *fcb, unsigned int block, char *buffer) CALLING;   /* write 128-byte block to file */

#endif

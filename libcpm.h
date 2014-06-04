#ifndef __LIBCPM_DOT_H__
#define __LIBCPM_DOT_H__

#define CPM_BLOCK_SIZE 128

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

void cpm_abort(void);
void cpm_f_prepare(cpm_fcb *fcb, char *name);         /* set filename in FCB, etc */
int cpm_f_delete(cpm_fcb *fcb);                       /* delete a file */
int cpm_f_open(cpm_fcb *fcb);                         /* open a file */
int cpm_f_create(cpm_fcb *fcb);                       /* create a file */
int cpm_f_close(cpm_fcb *fcb);                        /* close a file */
unsigned int cpm_f_getsize(cpm_fcb *fcb);             /* return file size, in 128-byte blocks */

/* sequential block I/O */
unsigned char cpm_f_read_next(cpm_fcb *fcb, char *buffer);      /* read the next 128-byte block from file */
unsigned char cpm_f_write_next(cpm_fcb *fcb, char *buffer);     /* write the next 128-byte block to file */

/* random block I/O */
unsigned char cpm_f_read_random(cpm_fcb *fcb, unsigned int block, char *buffer);    /* read 128-byte block from file */
unsigned char cpm_f_write_random(cpm_fcb *fcb, unsigned int block, char *buffer);   /* write 128-byte block to file */

#endif

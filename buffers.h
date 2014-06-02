#ifndef __BUFFERS_DOT_H__
#define __BUFFERS_DOT_H__

#include "libcpm.h"

/* storage for our buffers is defined in buffers.s so we can force them into the _BSS section */

#define FILEBUFFER_BLOCKS 32 /* must be a power of 2 and fit a whole setor for sector-at-once programming */

extern unsigned char filebuffer[CPM_BLOCK_SIZE * FILEBUFFER_BLOCKS];
extern unsigned char rombuffer[CPM_BLOCK_SIZE]; /* used by Z180 DMA as temporary holding space */

#endif

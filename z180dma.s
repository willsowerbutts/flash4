    .module z180dma
    .hd64

    .globl _dma_memory

_SAR0L	=	0x0060
_SAR0H	=	0x0061
_SAR0B	=	0x0062
_DAR0L	=	0x0063
_DAR0H	=	0x0064
_DAR0B	=	0x0065
_BCR0L	=	0x0066
_BCR0H	=	0x0067
_DSTAT	=	0x0070
_DMODE	=	0x0071
_DCNTL	=	0x0072

    .area _CODE

_dma_memory:
    push ix
    ld ix,#0
    add ix,sp

    ld a, 4(ix)
    out0 (_SAR0L), a
    ld a, 5(ix)
    out0 (_SAR0H), a
    ld a, 6(ix)
    out0 (_SAR0B), a

    ld  a, 8 (ix)
    out0 (_DAR0L), a
    ld  a, 9 (ix)
    out0 (_DAR0H), a
    ld  a, 10 (ix)
    out0 (_DAR0B), a

    ld  a, 12 (ix)
    out0 (_BCR0L), a
    ld  a, 13 (ix)
    out0 (_BCR0H), a

    ld  a, #0x02
    out0 (_DMODE),a
    ld  a, #0x41
    out0 (_DSTAT),a

    pop ix
    ret

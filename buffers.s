        .module buffers

        .globl _filebuffer
        .globl _rombuffer

; sdcc doesn't put buffers into _BSS so we end up huge chunks of nothing in our executable.
; we have to fix this up by hand.

        .area   _BSS
        ; note the booster does not copy data in this section

        ; keep these in sync with the definitions in flash4.c
_filebuffer: .ds (128 * 32)
_rombuffer:  .ds 128

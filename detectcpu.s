    .module detectcpu

    .globl _detect_z180_cpu

    .area _CODE

    .hd64 ; required for MLT instruction below

_detect_z180_cpu:
    ; Thanks go to John Coffman for this Z80/Z180 detection code
    ld de, #(7*256 + 73)
    mlt de                ; 7*73 --> 511
    inc e
    ld l, #0
    ret nz                ; Z80 -> return 0
    inc l                 ; Z180 -> return 1
    ret

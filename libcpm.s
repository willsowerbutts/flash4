    .module libcpm

    .globl _cpm_f_open
    .globl _cpm_f_create
    .globl _cpm_f_close
    .globl _cpm_f_delete
    .globl _cpm_f_read_next
    .globl _cpm_f_write_next
    .globl _cpm_f_read_random
    .globl _cpm_f_write_random
    .globl _cpm_f_getsize
    .globl _cpm_abort

    .area _CODE

_cpm_abort:
    ld c, #0
    jp 5

_cpm_f_create:
    ld c, #0x16             ; Function 22, Make File
    jr gocpm

_cpm_f_close:
    ld c, #0x10             ; Function 16, Close File
    jr gocpm

_cpm_f_delete:
    ld c, #0x13             ; Function 19, Delete File
    jr gocpm

_cpm_f_open:
    ld c, #0x0F             ; Function 15, Open File
gocpm: ; the rest of this routine is shared with the other simple CP/M calls
    pop hl ; return address
    pop de ; FCB address (argument)
    ; put the stack back
    push de
    push hl

    call 5                  ; call CP/M BDOS

    ld hl, #0               ; return 0 on success
    inc a                   ; A=0xFF means error, becomes 0
    ret nz
    dec hl                  ; return -1 on error
    ret

_cpm_f_getsize:
    pop hl                  ; return address
    pop de                  ; FCB address (argument)
    ; put the stack back
    push de
    push hl

    push de                 ; save FCB address
    ld c, #0x23             ; Function 35, Compute File Size
    call 5                  ; call CP/M BDOS
    pop de                  ; restore FCB address to DE

    ; there does not appear to be any "error" status return from function 35.
    ; some docs suggest A=0xFF indicates error but this is not borne out by testing.

    ld hl, #33              ; random offset is at offset 33, 34, 35 in FCB
    add hl, de

    ld e, (hl)              ; load size (in 128-byte blocks) into DE
    inc hl
    ld d, (hl)

    ex de, hl               ; return value in HL

    ret


_cpm_f_read_next:
    push ix
    ld ix, #0
    add ix, sp
    ; stack has: ix, return address, fcb,  buffer
    ;                                ix+4  ix+6

    ; set the CP/M DMA address to our buffer
    ld c, #0x1A             ; Function 26, Set DMA address
    ld e, 6(ix)
    ld d, 7(ix)
    call 5

    ld c, #0x14             ; Function 20, Read next record
    ld e, 4(ix)
    ld d, 5(ix)
    call 5

    ; return result code
    ld h, #0
    ld l, a

    pop ix
    ret

_cpm_f_write_next:
    push ix
    ld ix, #0
    add ix, sp
    ; stack has: ix, return address, fcb,  buffer
    ;                                ix+4  ix+6

    ; set the CP/M DMA address to our buffer
    ld c, #0x1A             ; Function 26, Set DMA address
    ld e, 6(ix)
    ld d, 7(ix)
    call 5

    ld c, #0x15             ; Function 21, Write next record
    ld e, 4(ix)
    ld d, 5(ix)
    call 5

    ; return result code
    ld h, #0
    ld l, a

    pop ix
    ret

_cpm_f_read_random:
    push ix
    ld ix, #0
    add ix, sp
    ; stack has: ix, return address, fcb,  block, buffer
    ;                                ix+4  ix+6   ix+8

    ; set the CP/M DMA address to our buffer
    ld c, #0x1A             ; Function 26, Set DMA address
    ld e, 8(ix)
    ld d, 9(ix)
    call 5

    ; load FCB address into DE
    ld e, 4(ix)
    ld d, 5(ix)

    ; adjust record number in FCB
    ld hl, #33              ; random offset is at offset 33, 34, 35 in FCB
    add hl, de
    ld a, 6(ix)
    ld (hl), a
    inc hl
    ld a, 7(ix)
    ld (hl), a

    ld c, #0x21             ; Function 33, Read random
    call 5

    ; return result code
    ld h, #0
    ld l, a

    pop ix
    ret

_cpm_f_write_random:
    push ix
    ld ix, #0
    add ix, sp
    ; stack has: ix, return address, fcb,  block, buffer
    ;                                ix+4  ix+6   ix+8

    ; set the CP/M DMA address to our buffer
    ld c, #0x1A             ; Function 26, Set DMA address
    ld e, 8(ix)
    ld d, 9(ix)
    call 5

    ; load FCB address into DE
    ld e, 4(ix)
    ld d, 5(ix)

    ; adjust record number in FCB
    ld hl, #33              ; random offset is at offset 33, 34, 35 in FCB
    add hl, de
    ld a, 6(ix)
    ld (hl), a
    inc hl
    ld a, 7(ix)
    ld (hl), a

    ld c, #0x22             ; Function 34, Write random
    call 5

    ; return result code
    ld h, #0
    ld l, a

    pop ix
    ret

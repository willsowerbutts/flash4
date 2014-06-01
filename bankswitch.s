    .module bankswitch

    .globl _romwbw_sys_getbnk
    .globl _romwbw_sys_setbnk
    .globl _flashrom_chip_read_bankswitch
    .globl _flashrom_chip_write_bankswitch
    .globl _flashrom_block_read_bankswitch
    .globl _flashrom_block_write_bankswitch
    .globl _default_mem_bank

ROMWBW_SETBNK .equ 0xFC06
ROMWBW_GETBNK .equ 0xFC09

    .area _CODE

_romwbw_sys_setbnk:
    ld hl, #2
    add hl, sp
    ld c, (hl)
    call #ROMWBW_SETBNK
    ret

_romwbw_sys_getbnk:
    call #ROMWBW_GETBNK
    ld l, a
    ret

selectaddr:
    ; 32-bit address is at sp+4 through sp+7
	ld hl,#6        ; we're interested initially in sp+6 and sp+5
	add	hl,sp
    ; compute (address >> 15) & 0x0f
    ld a, (hl)      ; top three bits we want are in here
    and #0x7        ; mask off low three bits
    add a, a        ; shift left one place
    dec hl          ; hl is now sp+5
    bit 7, (hl)     ; test top bit
    jr z, bankready
    or #1           ; set low bit if required
bankready:
    ; now A contains (address >> 15) & 0x0f -- let's select that bank!
    ld c, a
    push hl         ; we'll need this in a moment
    call #ROMWBW_SETBNK
    pop hl
    ld d, (hl)
    res 7, d
    dec hl          ; hl is now sp+4
    ld e, (hl)
    ; now DE contains the offset
    ret

_flashrom_chip_read_bankswitch:
    call selectaddr
    ld a, (de) ; read from the flash in banked memory
    ld l, a
putback:
    push hl
    ; put our memory back in the banked region
	ld a, (_default_mem_bank)
    ld c, a
    call #ROMWBW_SETBNK
    pop hl ; recover value read from flash (into L, H gets the F register)
    ret

_flashrom_chip_write_bankswitch:
    call selectaddr
    inc hl
    inc hl
    inc hl
    inc hl          ; hl is now sp+6 where the value to write resides
    ld a, (hl)
    ld (de), a      ; write to flash memory 
    jr putback

targetlength:
    ex de, hl       ; banked flash address -> hl
    push ix
    ld ix, #0
    add ix, sp
    ld e, 10(ix)    ; buffer address -> de
    ld d, 11(ix)
    ld c, 12(ix)    ; length -> bc
    ld b, 13(ix)
    pop ix
    ret

_flashrom_block_read_bankswitch:
    call selectaddr
    call targetlength
    ldir            ; copy copy copy
    jr putback

_flashrom_block_write_bankswitch:
    call selectaddr
    call targetlength
writenext:
    ; program a range of bytes in flash;
    ; DE = source address in RAM (pointer into buffer)
    ; HL = destination address in flash (pointer into banked memory)
    ; BC = byte counter (# bytes remaining to program)

    ; put flash into write mode
    ld a, #0xaa
    ld (0x5555), a
    ld a, #0x55
    ld (0x2aaa), a
    ld a, #0xa0
    ld (0x5555), a

    ; program the byte
    ld a, (de)
    ld (hl), a

    ; partial setup for the next byte while the write occurs
    dec bc
    inc de

    ; wait for programming to complete
writewait:
    ld a, (hl)
    cp (hl)
    jr nz, writewait
    ; data sheet advises double checking with two further reads
    ld a, (hl)
    cp (hl)
    jr nz, writewait

    ; finish setup for the next byte
    inc hl
    ld a, b
    or c
    jr nz, writenext
    jr putback

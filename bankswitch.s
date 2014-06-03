    .module bankswitch

    .globl _bankswitch_get_current_bank
    .globl _flashrom_chip_read_bankswitch
    .globl _flashrom_chip_write_bankswitch
    .globl _flashrom_block_read_bankswitch
    .globl _flashrom_block_write_bankswitch
    .globl _flashrom_block_verify_bankswitch
    .globl _default_mem_bank
    .globl _bank_switch_method

; RomWBW entry vectors
ROMWBW_SETBNK      .equ 0xFC06
ROMWBW_GETBNK      .equ 0xFC09

; UNA BIOS banked memory functions
UNABIOS_ENTRY      .equ 0x08 ; entry vector
UNABIOS_BANKEDMEM  .equ 0xFB ; C register - function number
UNABIOS_BANK_GET   .equ 0x00 ; B register - subfunction number
UNABIOS_BANK_SET   .equ 0x01 ; B register - subfunction number

    .area _CODE

    ; load the page in register HL into the banked region (lower 32K)
loadbank:
    ld a, (_bank_switch_method)
    or a
    jr z, loadbank_romwbw
    dec a
    jr z, loadbank_unabios
    ; well, this is unexpected
    ret
loadbank_romwbw:
    ld a, l
    call #ROMWBW_SETBNK
    ret
loadbank_unabios:
    ld bc, #(UNABIOS_BANK_SET << 8 | UNABIOS_BANKEDMEM)
    ex de, hl ; move page number into DE
    rst #UNABIOS_ENTRY
    ret

    ; return the currently loaded page number
_bankswitch_get_current_bank:
    ld a, (_bank_switch_method)
    or a
    jr z, getbank_romwbw
    dec a
    jr z, getbank_unabios
    ; well, this is unexpected
    ld hl, #0
    ret
getbank_romwbw:
    call #ROMWBW_GETBNK
    ; returns page number in A
    ld h, #0
    ld l, a
    ret
getbank_unabios:
    ld bc, #(UNABIOS_BANK_GET << 8 | UNABIOS_BANKEDMEM)
    rst #UNABIOS_ENTRY
    ; returns page number in DE
    ex de, hl
    ret

selectaddr:
    ; 32-bit address is at sp+4 through sp+7
    ld hl,#6        ; we're interested initially in sp+6 and sp+5
    add hl,sp
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
    push hl         ; we'll need this in a moment
    ld h, #0
    ld l, a
    call loadbank
    pop hl          ; recover pointer to data on stack
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
    ld hl, (_default_mem_bank)
    call loadbank
    pop hl ; recover value read from flash
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

_flashrom_block_verify_bankswitch:
    call selectaddr
    call targetlength
    ; DE = source address in RAM (pointer into buffer)
    ; HL = destination address in flash (pointer into banked memory)
    ; BC = byte counter (# bytes remaining to program)
cmpnext:
    ld a, b
    or c
    jr z, cmpok
    ld a, (de)
    cp (hl)
    jr nz, cmpfail
    inc de
    inc hl
    dec bc
    jr cmpnext
cmpok:
    ld l, #1        ; return true
    jr putback
cmpfail:
    ld l, #0        ; return false
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

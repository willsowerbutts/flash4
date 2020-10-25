    .module bankswitch

    .globl _bankswitch_get_current_bank
    .globl _bankswitch_get_rom_bank_count
    .globl _flashrom_chip_read_bankswitch
    .globl _flashrom_chip_write_bankswitch
    .globl _flashrom_block_read_bankswitch
    .globl _flashrom_block_write_bankswitch
    .globl _flashrom_block_verify_bankswitch
    .globl _default_mem_bank
    .globl _bank_switch_method
    .globl _una_entry_vector

; RomWBW entry vectors
ROMWBW_OLD_SETBNK  .equ 0xFC06  ; prior to v2.6
ROMWBW_OLD_GETBNK  .equ 0xFC09  ; prior to v2.6
ROMWBW_SETBNK      .equ 0xFFF3  ; v2.6 and later (function vector)
ROMWBW_CURBNK      .equ 0xFFE0  ; v2.6 and later (byte variable)
ROMWBW_MEMINFO     .equ 0xF8F1  ; v2.6 and later

; UNA BIOS banked memory functions
UNABIOS_ENTRY      .equ 0x08 ; entry vector
UNABIOS_BANKEDMEM  .equ 0xFB ; C register - function number
UNABIOS_BANK_GET   .equ 0x00 ; B register - subfunction number
UNABIOS_BANK_SET   .equ 0x01 ; B register - subfunction number

; P112 Z80182 hardware control registers
P112_DCNTL         .equ 0x32 ; Device control register
P112_BBR           .equ 0x39 ; Bank base register
P112_SCR           .equ 0xEF ; System config register

; N8VEM SBC hardware control registers
N8VEM_MPCL_RAM     .equ 0x78 ; IO address of RAM memory pager configuration latch
N8VEM_MPCL_ROM     .equ 0x7C ; IO address of ROM memory pager configuration latch

    .area _CODE
    .z180

    ; load the page in register HL into the banked region (lower 32K)
loadbank:
    ld a, (_bank_switch_method)
    or a
    jr z, loadbank_romwbw_old
    dec a
    jr z, loadbank_unabios
    dec a
    jr z, loadbank_p112
    dec a
    jr z, loadbank_romwbw_26
    dec a
    jr z, loadbank_n8vem_sbc
    ; well, this is unexpected
    ret
loadbank_n8vem_sbc:
    ld a, l
    out (N8VEM_MPCL_ROM), a
    out (N8VEM_MPCL_RAM), a
    ret
loadbank_romwbw_old:
    ld a, l
    call #ROMWBW_OLD_SETBNK
    ret
loadbank_romwbw_26:
    ld a, l
    call #ROMWBW_SETBNK
    ret
loadbank_unabios:
    ; This is slightly tricky. Normally we'd enter UNA via the RST 8 call,
    ; but we can't do this as we're going to replace the lower part of memory
    ; with flash ROM contents, that does not have the RST 8 vector in place.
    ld bc, #(UNABIOS_BANK_SET << 8 | UNABIOS_BANKEDMEM)
    ex de, hl                   ; move page number into DE
    ld hl, #loadbank_una_return
    push hl                     ; put return address on stack
    ld hl, (_una_entry_vector)
    jp (hl)                     ; simulate call to entry vector
loadbank_una_return:
    ret
loadbank_p112:
    ; map in the EEPROM if HL==0, else unmap it
    ld a, h
    or l
    jr nz, unmap_p112_rom
map_p112_rom:
    di                  ; disable interrupts
    xor a
    out0 (P112_BBR), a  ; map ROM into banked area
    in0 a, (P112_SCR)
    and a, #0xf7        ; enable EEPROM
    out0 (P112_SCR), a
    in0 a, (P112_DCNTL)
    or #0xc0            ; set 3 memory wait states
    out0 (P112_DCNTL), a
    ret
unmap_p112_rom:
    out0 (P112_BBR), l
    in0 a, (P112_SCR)
    or a, #0x08         ; disable EEPROM
    out0 (P112_SCR), a
    out0 (P112_DCNTL), h
    ei                  ; interrupts back on
    ret

_bankswitch_get_rom_bank_count:
    ld a, (_bank_switch_method)
    cp #3               ; romwbw 2.6+?
    jr nz, retzero      ; return 0 if not
    ld bc, #ROMWBW_MEMINFO ; SYSGET MEMINFO
    rst 8               ; call into RomWBW
    or a                ; A=0?
    jr nz, retzero      ; something went wrong
    ld h, #0
    ld l, d             ; return number of ROM banks in HL
    ret

    ; return the currently loaded page number
_bankswitch_get_current_bank:
    ld a, (_bank_switch_method)
    or a
    jr z, getbank_romwbw_old
    dec a
    jr z, getbank_unabios
    dec a
    jr z, getbank_p112
    dec a
    jr z, getbank_romwbw_26
    dec a
    jr z, getbank_n8vem_sbc
    ; well, this is unexpected
retzero:
    ld hl, #0
    ret
getbank_n8vem_sbc:
    ld hl, #0x0080      ; we assume that it's the first page of RAM
    ret
getbank_romwbw_old:
    call #ROMWBW_OLD_GETBNK
    ; returns page number in A
    ld h, #0
    ld l, a
    ret
getbank_romwbw_26:
    ld a, (ROMWBW_CURBNK)
    ld h, #0
    ld l, a
    ret
getbank_unabios:
    ld bc, #(UNABIOS_BANK_GET << 8 | UNABIOS_BANKEDMEM)
    ld hl, #getbank_una_return
    push hl                     ; put return address on stack
    ld hl, (_una_entry_vector)
    jp (hl)                     ; simulate call to entry vector
getbank_una_return:
    ; returns page number in DE
    ex de, hl
    ret
getbank_p112:
    in0 h, (P112_DCNTL)
    in0 l, (P112_BBR)
    ret

selectaddr:
    ; 32-bit address is at sp+4 through sp+7
    ; The code below is limited to 256 x 32KB banks = 8MB
    ; eg for flash address = 0x654321:
    ; mem addr: SP+4 SP+5 SP+6 SP+7
    ; contents:  21   43   65   00
    ; bank number = address >> 15    = 0xCA   -- data in SP+6 (7 bits), SP+5 (1 bit)
    ; bank offset = address & 0x7FFF = 0x4321 -- data in SP+5 (7 bits), SP+4 (8 bits)

    ; first compute bank number
    ld hl,#6
    add hl,sp       ; HL is now SP+6
    ld a, (hl)      ; 7 bits we want are in here
    add a, a        ; shift left one place
    dec hl          ; HL is now SP+5
    bit 7, (hl)     ; test top bit
    jr z, bankready
    or #1           ; set low bit if required
bankready:
    ; now A contains desired bank number -- let's select that bank!
    push hl         ; stash this, we'll need it in a moment
    ld h, #0        ; zero high byte
    ld l, a         ; bank number now in HL
    call loadbank
    pop hl          ; HL is now SP+5 again
    ; now compute bank offset
    ld d, (hl)      ; load top byte of the word
    res 7, d        ; clear out the top bit (already used for the bank number)
    dec hl          ; hl is now sp+4
    ld e, (hl)      ; low low byte of the word
    ret             ; return with bank selected, DE containing the offset (range 0--0x7FFF)

_flashrom_chip_read_bankswitch:
    call selectaddr
    ld a, (de) ; read from the flash in banked memory
    ld l, a
    jr putback

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
putback:
    push hl
    ; put our memory back in the banked region
    ld hl, (_default_mem_bank)
    call loadbank
    pop hl ; recover value read from flash
    ret

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
nextbyte:
    dec bc
    inc de

    ; check if the next byte is 0xff -- we can skip it (0xff = erased)
    ld a, (de)
    inc a
    jr nz, writewait ; only 0xff + 1 = 0
    ld a, b          ; but are there any bytes left to write?
    or c
    jr z, writewait  ; if this was the last byte, complete normally
    inc hl           ; data sheet does not indicate we must read status from the same address
    jr nextbyte      ; okay, we can safely skip this byte!

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
    inc hl           ; do not do this earlier, to ensure we stay in the ROM address space
    ld a, b
    or c
    jr nz, writenext
    jr putback

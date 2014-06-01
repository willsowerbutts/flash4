    .module putchar
    .globl _putchar
    .area _CODE

; putchar routine for CP/M
_putchar:
    ld hl,#2
    add hl,sp

    push hl
    ld e,(hl)
    ld c,#2
    call 5
    pop hl

    ; we even handle CRLF correctly
    ld a, (hl)
    cp #10
    ret nz
    ld e, #13
    ld c, #2
    call 5

    ret

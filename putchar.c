#include "calling.h"

int putchar(int c)
{
#ifdef REGISTER_CALLING
__asm
; putchar routine for CP/M -- sdcccall(1) version
    ; char to print is in L
    push hl
    ld e,l
    ld c,#2
    call 5
    pop hl

    ; we even handle CRLF correctly
    ld a, l
    cp #10
    ret nz
    ld e, #13
    ld c, #2
    call 5

    ret
__endasm;
#else
__asm
; putchar routine for CP/M -- sdcccall(0) version
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
__endasm;
#endif
    /* this code is compiled but never used.
     * it exists to squelch compiler warnings.
     */
    c;
    return 0;
}

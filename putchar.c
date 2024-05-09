#include "calling.h"

int putchar(int c)
{
#ifdef REGISTER_CALLING
__asm
; putchar routine for CP/M -- sdcccall(1) version
    ; char to print is in L
    push hl

    ; print char
    ld e,l
    ld c,#2
    call 5

    ; check for CR
    pop hl
    ld a, l
    cp #10
    ret nz      ; return if not CR

    ; after CR always print LF
    ld e, #13
    ld c, #2
    jp 5        ; leave our return address on the stack
__endasm;
#else
__asm
; putchar routine for CP/M -- sdcccall(0) version
    ; char to print is in (SP+2)
    ld hl,#2
    add hl,sp
    push hl

    ; print char
    ld e,(hl)
    ld c,#2
    call 5

    ; check for CR
    pop hl
    ld a, (hl)
    cp #10
    ret nz      ; return if not CR

    ; after CR always print LF
    ld e, #13
    ld c, #2
    jp 5        ; leave our return address on the stack
__endasm;
#endif
    /* this code is compiled but never used.
     * it exists to squelch compiler warnings.
     */
    c;
    return 0;
}

; Z80 CP/M CRT0 for SDCC
; 2013-12-16, 2014-05-26  William R Sowerbutts

        .module runtime0
        .globl _main
        .globl l__INITIALIZER
        .globl s__INITIALIZED
        .globl s__INITIALIZER
        .globl s__GSFINAL

        .area _CODE
        ; this code is loaded at 0x100 by CP/M but is linked to run at 0x8000 above the
        ; bank switched memory region. 
        
        ; copy ourselves to the right location.
booster:
        ; we're executing at 0x100 
        ld hl, #0x0100
        ld de, #0x8000
        ld bc, #(init_stackptr-0x8100)
        ldir

        ; jump into the new copy
        jp stage2 
stage2:

        ; wipe out the original copy
        ld de, #0x101
        ld hl, #0x100
        ld a, #0xe5
        ld bc, #(init_stackptr-0x8101)
        ld (hl), a
        ldir

        ; and we're off!

init:
        ld  sp, #init_stackptr
    
        ; Initialise global variables
        call    gsinit

        ; Parse the command line
        ld hl, #0x0081          ; first byte of command line
        ld ix, #(argv_ptr+2)    ; pointer to argument list in IX
        ld iy, #1               ; we always have 1 argument (program name)

cmdnextarg:
        ld b, h                 ; store pointer to start of argument in BC
        ld c, l

cmdnextbyte:
        ld a, (hl)              ; read next command line byte
        or a                    ; NUL -- end of command line?
        jr z, endcmdline
        cp #' '                 ; space -- end of this argument?
        jr z, cmdgotone
        inc hl
        jr cmdnextbyte          ; loop

cmdgotone:
        xor a                   ; replace space with NUL
        ld (hl), a
        call depositarg
        inc hl
        jr cmdnextarg

endcmdline:
        call depositarg         ; deposit final argument
        ld hl, #argv_ptr        ; pointer to argument list
        push hl                 ; argv
        push iy                 ; argc
    
        ; Call the C main() routine
        call _main
    
        ; Terminate after main() returns
        ld  c, #0
        call 5

depositarg:
        ld a, (bc)              ; is the argument empty?
        or a
        ret z                   ; it was empty -- no action
        ld 0(ix), c             ; store pointer
        ld 1(ix), b
        inc ix                  ; inc argv
        inc ix
        inc iy                  ; inc argc
        ret
    
        ; Ordering of segments for the linker.
        ; WRS: Note we list all our segments here, even though
        ; we don't use them all, because ordering is set
        ; when they are first seen, it would appear.
        .area   _TPA
    
        .area   _HOME
        .area   _CODE
        .area   _INITIALIZER
        .area   _GSINIT
        .area   _GSFINAL
    
        .area   _DATA
        .area   _INITIALIZED
        .area   _BSEG
        .area   _STACK
        .area   _BSS
        .area   _HEAP

; ----------------------------------------
        .area   _STACK
        ; we keep argv[] in here as well, it grows up while the stack grows down
progname: 
        .ascii "CPMPROG"
        .db 0
argv_ptr:
        .dw #progname ; first entry of argv vector
        .ds 256   ; stack memory
init_stackptr:    ; this is the last thing the booster code will copy

; ----------------------------------------
        .area   _GSINIT
gsinit::
        ld      bc, #l__INITIALIZER
        ld      a, b
        or      a, c
        jr      z, gsinit_next
        ld      de, #s__INITIALIZED
        ld      hl, #s__INITIALIZER
        ldir
gsinit_next:

; ----------------------------------------
        .area   _GSFINAL
        ret

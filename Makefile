SDAS=sdasz80
SDCC=sdcc
SDLD=sdldz80
SDASOPTS=-plosff

SDCCOPTS=--std-sdcc99 --no-std-crt0 -mz80 --opt-code-size --max-allocs-per-node 10000 --Werror --stack-auto

CSRCS =  flash4.c libcpm.c z180dma2.c
ASRCS =  runtime0.s putchar.s libcpm2.s z180dma.s bankswitch.s detectcpu.s buffers.s

COBJS = $(CSRCS:.c=.rel)
AOBJS = $(ASRCS:.s=.rel)
OBJS  = $(AOBJS) $(COBJS) 

JUNK = $(CSRCS:.c=.lst) $(CSRCS:.c=.asm) $(CSRCS:.c=.sym) $(ASRCS:.s=.lst) $(ASRCS:.s=.sym) $(CSRCS:.c=.rst) $(ASRCS:.s=.rst)

all:	flash4.com

.SUFFIXES:		# delete the default suffixes
.SUFFIXES: .c .s .rel

$(COBJS): %.rel: %.c
	$(SDCC) $(SDCCOPTS) -c $<

$(AOBJS): %.rel: %.s
	$(SDAS) $(SDASOPTS) $<

clean:
	rm -f $(OBJS) $(JUNK) *~ flash4.com flash4.ihx flash4.map

flash4.com: $(OBJS)
	$(SDLD) -nmwx -i flash4.ihx -b _CODE=0x8000 -k /usr/local/share/sdcc/lib/z80/ -l z80 $(OBJS)
	srec_cat -disable-sequence-warning flash4.ihx -intel -offset -0x8000 -output flash4.com -binary

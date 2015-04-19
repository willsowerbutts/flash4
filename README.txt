
           FLASH4 (c) 2014 William R Sowerbutts <will@sowerbutts.com>
                          http://sowerbutts.com/8bit/

= Warning =

FLASH4 has been tested and confirmed working on:
 * N8VEM SBCv2
 * N8VEM N8-2312
 * N8VEM Mark IV SBC
 * DX-Designs P112
 * ZETA SBC v2

However it remains somewhat experimental. If it works for you, please let me
know. If it breaks please also let me know so I can fix it!


= Introduction =

FLASH4 is a CP/M program which can read, write and verify Flash ROM contents to
or from an image file stored on a CP/M filesystem. It is intended for in-system
programming of Flash ROM chips on Z80 and Z180 systems.

FLASH4 aims to support a range of Flash ROM chips. Ideally I would like to
support all Flash ROM chips that are in use in Z80/Z180 N8VEM machines. If
FLASH4 does not support your chip please let me know and I will try to add
support.

When writing to the Flash ROM chip, FLASH4 will only reprogram the sectors
whose contents have changed. This helps to reduce wear on the flash memory,
makes the reprogram operation faster, and reduces the risk of leaving the
system unbootable if power fails during a reprogramming operation. FLASH4
always performs a full verify operation after writing to the chip to confirm
that the correct data has been loaded.

FLASH4 is reasonably fast. Reprogramming and verifying every sector on a 512KB
SST 39F040 chip takes 21 seconds on my Mark IV SBC, versus 45 seconds to
perform the same task using a USB MiniPro TL866 EEPROM programmer under Linux
on my PC. If only a subset of sectors require reprogramming FLASH4 will be
even faster.

FLASH4 works with binary ROM image files, it does not support Intel Hex format
files. Hex files can be easily converted to or from binaries using "hex2bin" or
the "srec_cat" program from SRecord:

  $ srec_cat image.hex -intel -fill 0xFF 0 0x80000 -output image.bin -binary
  $ srec_cat image.bin -binary -output image.hex -intel

FLASH4 can use several different methods to access the Flash ROM chip. The best
available method is determined automatically at run time. Alternatively you may
provide a command-line option to force the use of a specific method.

The first two methods use bank switching to map sections of the ROM into the
CPU address space. FLASH4 will detect the presence of RomWBW or UNA BIOS and
use the bank switching methods they provide. 

On P112 systems the P112 B/P BIOS is detected and P112 bank switching is used.

If no bank switching method can be auto-detected, and the system has a Z180
CPU, FLASH4 will use the Z180 DMA engine to access the Flash ROM chip. This
does not require any bank switching but it is slower and will not work on all
platforms.

Z180 DMA access requires the flash ROM to be linearly mapped into the lower
region of physical memory, as it is on the Mark IV SBC (for example). The
N8-2312 has additional memory mapping hardware, consequently Z180 DMA access on
the N8-2312 is NOT SUPPORTED and if forced will corrupt the contents of RAM;
use one of the supported bank switching methods instead.

Z180 DMA access requires the Z180 CPU I/O base control register configured to
locate the internal I/O addresses at 0x40 (ie ICR bits IOA7, IOA6 = 0, 1).


= Usage =

The three basic operations are:

  FLASH4 WRITE filename [options]

This will rewrite the flash ROM contents from the named file. The file size
must exactly match the size of the ROM chip. After the write operation, a
verify operation will be performed automatically.

  FLASH4 VERIFY filename [options]

This will read out the flash ROM contents and report if it matches the contents
of the named file. The file size must exactly match the size of the ROM chip.

  FLASH4 READ filename [options]

This will read out the entire flash ROM contents and write it to the named
file.

If your ROM chip is larger than the image you wish to write, use the "/PARTIAL"
(or "/P") command line option. To avoid accidentally flashing the wrong file,
the image file must be an exact multiple of 32KB in length. The portion of the
ROM not occupied by the image file is left either unmodified or erased.

If you are using an ROM/EPROM/EEPROM chip which cannot be programmed in-system,
FLASH4 will not be able to recognise it, however the software can still
usefully READ and VERIFY the chip. Use the "/ROM" command line option to enable
"READ" or "VERIFY" mode with unrecognised chips. This mode assumes a 512K ROM
is fitted; smaller ROMs will be treated as a 512K ROM with the data repated
multiple times -- with a 256K chip the data is repeated twice, four times for a
128K chip, etc.

One of the following optional command line arguments may be specified at the
end of the command line to force FLASH4 to use a particular method to access
the flash ROM chip:

  /ROMWBW         For ROMWBW BIOS version 2.6 and later
  /ROMWBWOLD      For ROMWBW BIOS version 2.5 and earlier
  /UNABIOS        For UNA BIOS
  /Z180DMA        For Z180 DMA
  /P112           For DX-Designs P112

If no option is specified FLASH4 attempts to determine the best available
method automatically.


= Supported chips and features =

FLASH4 will interrogate your flash ROM chip to identify it automatically.
FLASH4 assumes that you have a single flash ROM device and it is located at the
bottom of the physical memory map.

FLASH4 does not support setting or resetting the protection bits on individual
sectors within Flash ROM devices. If your Flash ROM chip has protected sectors
you will need to unprotect them by other means before FLASH4 can erase and
reprogram them.

AT29C series chips employ an optional "software data protection" feature. This
is supported by FLASH4 and is left activated after programming the chip to
prevent accidental reprogramming of sectors.

The following chips are supported:

  AT29F010
  AT29F040
  M29F010
  M29F040
  MX29F040
  SST 39F010
  SST 39F020
  SST 39F040
  AT29C512
  AT29C040
  AT29C010
  AT29C020

The following chips are supported but have unequal sector sizes; FLASH4 will
only erase and reprogram the entire chip at once rather than its normal
sector-by-sector operation:

  AT49F001NT
  AT49F001N
  AT49F002N
  AT49F002NT
  AT49F040

If you use a flash ROM chip that is not listed above please email me
(will@sowerbutts.com) and I will try to add support for it.


= Compiling =

The software is written in a mix of C and assembler. It builds using the SDCC
toolchain and the SRecord tools. A Makefile is provided to build the executable
in Linux and I imagine it can be easily modified to build in Windows.

You may need to adjust the path to the SDCC libraries in the Makefile if your
sdcc installation is not in /usr/local


= License =

FLASH4 is licensed under the The GNU General Public License version 3 (see
included "LICENSE.txt" file). 

FLASH4 is provided with NO WARRANTY. In no event will the author be liable for
any damages. Use of this program is at your own risk. May cause rifts in space
and time.

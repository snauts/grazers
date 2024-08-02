ARCH ?= -mz80

CFLAGS += --nostdinc --nostdlib --no-std-crt0
CFLAGS += --code-loc $(CODE) --data-loc $(DATA)

ENTRY = grep _reset grazers.map | cut -d " " -f 6

all:
	@echo "make zxs" - build .tap for ZX Spectrum
	@echo "make sms" - build .sms for Sega Masters
	@echo "make msx" - build .rom for MSX computer
	@echo "make c64" - build .prg for C64 computer
	@echo "make fuse" - build and run fuse
	@echo "make mame" - build and run mame
	@echo "make blast" - build and run blastem
	@echo "make open" - build and run openmsx
	@echo "make vice" - build and run vice

pcx:
	@gcc $(TYPE) -lm pcx-dump.c -o pcx-dump
	@./pcx-dump -c tiles.pcx > data.h
	@./pcx-dump -c fence.pcx >> data.h
	@./pcx-dump -l dialog.pcx >> data.h
	@./pcx-dump -l quarantine.pcx >> data.h
	@./pcx-dump -l gardener.pcx >> data.h
	@./pcx-dump -l earthquake.pcx >> data.h
	@./pcx-dump -l flooding.pcx >> data.h
	@./pcx-dump -l tsunami.pcx >> data.h
	@./pcx-dump -l equilibrium.pcx >> data.h
	@./pcx-dump -l migration.pcx >> data.h
	@./pcx-dump -l aridness.pcx >> data.h
	@./pcx-dump -l lonesome.pcx >> data.h
	@./pcx-dump -l eruption.pcx >> data.h
	@./pcx-dump -l fertility.pcx >> data.h
	@./pcx-dump -l erosion.pcx >> data.h
	@./pcx-dump -c logo.pcx >> data.h
	@./pcx-dump -l logo.pcx >> data.h
	@./pcx-dump -c sunset.pcx >> data.h
	@./pcx-dump -l sunset.pcx >> data.h
	@./pcx-dump -c volcano.pcx >> data.h
	@./pcx-dump -l volcano.pcx >> data.h

prg:
	@sdcc $(ARCH) $(CFLAGS) $(TYPE) main.c -o grazers.ihx
	hex2bin grazers.ihx > /dev/null

tap:
	bin2tap -b -r $(shell printf "%d" 0x$$($(ENTRY))) grazers.bin

zxs:
	TYPE=-DZXS make pcx
	CODE=0x8000 DATA=0xe000	TYPE=-DZXS make prg
	@make tap

fuse: zxs
	fuse --no-confirm-actions -g 2x grazers.tap

sms:
	TYPE=-DSMS make pcx
	@./pcx-dump -s font.pcx >> data.h
	CODE=0x0000 DATA=0xc000	TYPE=-DSMS make prg
	gcc mkrom.c -o mkrom
	./mkrom

mame: sms
	mame -w -r 640x480 sms -cart grazers.sms

blast: sms
	blastem grazers.sms

msx:
	TYPE=-DMSX make pcx
	CODE=0x4000 DATA=0xc000	TYPE=-DMSX make prg
	dd if=/dev/zero of=grazers.rom bs=1024 count=32
	dd if=grazers.bin of=grazers.rom conv=notrunc

open: msx
	openmsx grazers.rom

MOS6502_CFLAGS = --nostdinc --nostdlib --no-std-crt0 --no-zp-spill

c64:
	TYPE=-DC64 make pcx
	@sdcc -mmos6502 -DC64 $(MOS6502_CFLAGS) main.c -c
	@sdld -b CODE=0x7ff -b BSS=0x6c00 -b ZP=0x2 -m -i grazers.ihx main.rel
	hex2bin -e prg grazers.ihx > /dev/null
	c1541 -format grazers,00 d64 grazers.d64 \
		-attach grazers.d64 -write grazers.prg grazers

vice: c64
	x64 -autostartprgmode 1 +confirmonexit grazers.prg

manual:
	magick logo.pcx logo.png
	magick tiles.pcx tiles.png
	magick fence.pcx fence.png
	pdflatex manual.tex
	evince manual.pdf

clean:
	rm -f grazers* pcx-dump tileset.bin data.h mkrom \
		*.log *.aux *.png *.pdf *.asm *.lst *.rel *.sym

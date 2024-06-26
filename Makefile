CFLAGS += -mz80 --nostdinc --nostdlib --no-std-crt0
CFLAGS += --code-loc $(CODE) --data-loc $(DATA)

ENTRY = grep _reset grazers.map | cut -d " " -f 6

all:
	@echo "make zxs" - build .tap for ZX Spectrum
	@echo "make sms" - build .sms for Sega Masters
	@echo "make fuse" - build and run fuse
	@echo "make mame" - build and run mame
	@echo "make blast" - build and run blastem

pcx:
	@gcc $(TYPE) -lm pcx-dump.c -o pcx-dump

prg:
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
	@./pcx-dump -c logo.pcx no-color >> data.h
	@./pcx-dump -l logo.pcx >> data.h
	@./pcx-dump -c sunset.pcx >> data.h
	@./pcx-dump -l sunset.pcx >> data.h
	@./pcx-dump -c volcano.pcx >> data.h
	@./pcx-dump -l volcano.pcx >> data.h
	@sdcc $(CFLAGS) $(TYPE) main.c -o grazers.ihx
	hex2bin grazers.ihx > /dev/null

tap:
	bin2tap -b -r $(shell printf "%d" 0x$$($(ENTRY))) grazers.bin

zxs: pcx
	CODE=0x8000 DATA=0xe000	TYPE=-DZXS make prg
	@make tap

fuse: zxs
	fuse --no-confirm-actions -g 2x grazers.tap

sms: pcx
	CODE=0x0000 DATA=0xC000	TYPE=-DSMS make prg
	gcc mkrom.c -o mkrom
	./mkrom

mame: sms
	mame -w -r 640x480 sms -cart grazers.sms

blast: sms
	blastem grazers.sms

clean:
	rm -f grazers* pcx-dump tileset.bin data.h mkrom

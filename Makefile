CFLAGS += -mz80 --nostdinc --nostdlib --no-std-crt0
CFLAGS += --code-loc $(CODE) --data-loc $(DATA)

ENTRY = grep _reset grazers.map | cut -d " " -f 6

all:
	@echo "make zxs" - build .tap for ZX Spectrum
	@echo "make fuse" - build and run fuse

prg:
	@gcc $(TYPE) -lm tga-dump.c -o tga-dump
	@sdcc $(CFLAGS) $(TYPE) main.c -o grazers.ihx
	hex2bin grazers.ihx > /dev/null

tap:
	bin2tap -b -r $(shell printf "%d" 0x$$($(ENTRY))) grazers.bin

zxs:
	CODE=0x8000 DATA=0xf000	TYPE=-DZXS make prg
	@make tap

fuse: zxs
	fuse --no-confirm-actions -g 2x grazers.tap

clean:
	rm -f grazers* tga-dump

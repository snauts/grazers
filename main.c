typedef signed char int8;
typedef unsigned char byte;
typedef unsigned short word;

#include "data.h"

#define ADDR(obj)	((word) (obj))
#define BYTE(addr)	(* (volatile byte *) (addr))
#define WORD(addr)	(* (volatile word *) (addr))
#define SIZE(array)	(sizeof(array) / sizeof(*(array)))

#ifdef ZXS
#define is_vsync()	vblank
#define SETUP_STACK()	__asm__("ld sp, #0xfdfc")
#define IRQ_BASE	0xfe00
#endif

static volatile byte vblank;
static byte *map_y[192];

static byte forest[768];

static byte *update[256];
static byte *mirror[256];

static void interrupt(void) __naked {
#ifdef ZXS
    __asm__("di");
    __asm__("push af");
    __asm__("ld a, #1");
    __asm__("ld (_vblank), a");
    __asm__("pop af");
    __asm__("ei");
#endif
    __asm__("reti");
}

static void __sdcc_call_hl(void) __naked {
    __asm__("jp (hl)");
}

static void setup_irq(byte base) {
    __asm__("di");
    __asm__("ld i, a"); base;
    __asm__("im 2");
    __asm__("ei");
}

static void out_fe(byte data) __naked {
    __asm__("out (#0xfe), a"); data;
    __asm__("ret");
}

static byte in_fe(byte a) __naked {
    __asm__("in a, (#0xfe)"); a;
    __asm__("ret");
}

static void memset(byte *ptr, byte data, word len) {
    while (len-- > 0) { *ptr++ = data; }
}

static void setup_system(void) {
    byte top = (byte) ((IRQ_BASE >> 8) - 1);
    word jmp_addr = (top << 8) | top;
    BYTE(jmp_addr + 0) = 0xc3;
    WORD(jmp_addr + 1) = ADDR(&interrupt);
    memset((byte *) IRQ_BASE, top, 0x101);
    setup_irq(IRQ_BASE >> 8);
}

static void clear_screen(void) {
#ifdef ZXS
    memset((byte *) 0x5800, 0x00, 0x300);
    memset((byte *) 0x4000, 0x00, 0x1800);
    out_fe(0);
#endif
}

#define DEBUG
static void wait_vblank(void) {
#ifdef DEBUG
    byte prev, next = 0;
    do {
	prev = next;
	next = in_fe(0x7f) & 1;
    } while (next == prev || prev == 0);
#endif
    while (!is_vsync()) { }
    vblank = 0;
}

static void precalculate(void) {
    for (byte y = 0; y < 192; y++) {
#ifdef ZXS
	byte f = ((y & 7) << 3) | ((y >> 3) & 7) | (y & 0xc0);
	map_y[y] = (byte *) (0x4000 + (f << 5));
#endif
    }
}

static void put_char(char symbol, word n, byte color) {
#ifdef ZXS
    byte x = n & 0x1f;
    byte y = (n >> 2) & ~7;
    BYTE(0x5800 + n) = color;
    byte *addr = (byte *) 0x3c00 + (symbol << 3);
    for (byte i = 0; i < 8; i++) {
	map_y[y + i][x] = *addr++;
    }
#endif
}

static void put_str(const char *msg, word n, byte color) {
    while (*msg != 0) {
	put_char(*(msg++), n++, color);
    }
}

static char to_hex(byte digit) {
    return (digit < 10) ? '0' + digit : 'A' + digit - 10;
}

static void put_num(word num, word n, byte color) {
    char msg[] = "0000";
    for (byte i = 0; i < 4; i++) {
	msg[3 - i] = to_hex(num & 0xf);
	num = num >> 4;
    }
    put_str(msg, n, color);
}

static void put_tile(byte cell, word n) {
    byte x = n & 0x1f;
    byte y = (n >> 2) & ~7;
    byte color = cell & 0xc ? 7 : 4;
    BYTE(0x5800 + n) = color;
    byte *addr = (byte *) tiles + (cell << 3);
    for (byte i = 0; i < 8; i++) {
	map_y[y + i][x] = *addr++;
    }
}

static const int8 neighbors[] = { -1, -32, 1, 32 };

static inline byte should_regrow(byte *ptr) {
    for (byte n = 0; n < SIZE(neighbors); n++) {
	byte near = *(ptr + neighbors[n]) & 0xf;
	if (0 < near && near <= 3) return 1;
    }
    return 0;
}

static byte **queue;
static inline void regrow_neighbors(byte *ptr) {
    for (byte n = 0; n < SIZE(neighbors); n++) {
	byte *near = ptr + neighbors[n];
	if (*near == 0) *(queue++) = near;
    }
}

static void update_grass(byte cell, byte *ptr) {
    switch(cell) {
    case 0:
	if (should_regrow(ptr)) goto grow;
    case 3:
	return;
    default:
	regrow_neighbors(ptr);
    }
  grow:
    *(queue++) = ptr;
    (*ptr)++;
}

static byte migrate(byte cell, byte *ptr) {
    for (byte n = 0; n < SIZE(neighbors); n++) {
	byte *near = ptr + neighbors[n];
	byte info = *near & 0xf;
	if (info <= 3 && info > 0) {
	    if (n == 0) cell |= 0x10;
	    if (n == 2) cell &= ~0x10;
	    *(queue++) = near;
	    *near |= cell;
	    return 1;
	}
    }
    return 0;
}

static void update_sheep(byte cell, byte *ptr) {
    byte food = cell & 0x3;
    byte size = cell & 0xc;
    if (food == 0) {
	cell = migrate(cell - 4, ptr) ? 0 : cell - 4;
    }
    else if (size < 0xc) {
	cell += 3; /* inc size +4, dec food -1 */
    }
    else {
	cell -= migrate(4, ptr) ? 4 : 1;
    }
    *(queue++) = ptr;
    *ptr = cell;
}

static void update_cell(byte *ptr) {
    byte cell = *ptr & 0x1f;

    if (cell & 0xc) {
	update_sheep(cell, ptr);
    }
    else {
	update_grass(cell, ptr);
    }

    put_tile(cell, ptr - forest);
}

static void clean_tags(byte **ptr) {
    while (*ptr) {
	*(*ptr++) &= ~0x40;
    }
}

static void advance_forest(byte **ptr) {
    clean_tags(ptr);
    while (*ptr) {
	byte *place = *ptr++;
	if ((*place & 0x40) == 0) {
	    update_cell(place);
	    *place |= 0x40;
	}
    }
    *queue = 0;
    wait_vblank();
}

static void add_fence(word n) {
    put_char('#', n, 4);
    forest[n + 0x000] = 0x80;
}

static void update_border(void) {
    for (word x = 0; x < 32; x++) {
	add_fence(0x000 + x);
	add_fence(0x2e0 + x);
    }
    for (word y = 0; y < 768; y += 32) {
	add_fence(0x000 + y);
	add_fence(0x01f + y);
    }
    for (word x = 0; x < 24; x++) {
	for (word y = 11; y < 13; y++) {
	    add_fence((y * 32) + x);
	}
    }
}

static void game_loop(void) {
    memset(update, 0x00, sizeof(update));
    memset(mirror, 0x00, sizeof(mirror));
    memset(forest, 0x00, sizeof(forest));
    update_border();

    forest[0x21] = 0x01;
    update[0x00] = forest + 0x21;

    forest[0x43] = 0x07;
    update[0x01] = forest + 0x43;

    for (;;) {
	queue = mirror;
	advance_forest(update);
	queue = update;
	advance_forest(mirror);
    }
}

void reset(void) {
    SETUP_STACK();
    setup_system();
    clear_screen();
    precalculate();

    game_loop();
}

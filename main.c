typedef signed char int8;
typedef unsigned char byte;
typedef unsigned short word;

#include "data.h"

#define ADDR(obj)	((word) (obj))
#define BYTE(addr)	(* (volatile byte *) (addr))
#define WORD(addr)	(* (volatile word *) (addr))
#define SIZE(array)	(sizeof(array) / sizeof(*(array)))

#define BIT(n)		(1 << (n))
#define TRUE		1
#define FALSE		0

#define C_FOOD		0x3
#define C_SIZE		0xc
#define C_FACE		BIT(4)
#define C_PLAY		BIT(5)
#define C_DONE		BIT(6)
#define C_TILE		BIT(7)

#ifdef ZXS
#define is_vsync()	vblank
#define SETUP_STACK()	__asm__("ld sp, #0xfdfc")
#define IRQ_BASE	0xfe00
#endif

static volatile byte vblank;
static byte *map_y[192];

static byte forest[768];

static byte *update[512];
static byte *mirror[512];

static word pos;

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

static byte in_1f(void) __naked {
    __asm__("ld bc, #0x1f");
    __asm__("in a, (c)");
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

static byte inc10_byte(byte *num) {
    byte value = *num + 0x01;
    if ((value & 0xf) > 0x09) {
	value = (value & 0xf0) + 0x10;
	if (value > 0x99) value = 0;
    }
    *num = value;
    return value;
}

static word inc10(word num) {
    byte *ptr = (byte *) &num;
    if (inc10_byte(ptr + 0) == 0) {
	inc10_byte(ptr + 1);
    }
    return num;
}

static byte *sprites;
static void put_tile(byte cell, byte color, word n) {
    byte x = n & 0x1f;
    byte y = (n >> 2) & ~7;
    BYTE(0x5800 + n) = color;
    byte *addr = sprites + (cell << 3);
    for (byte i = 0; i < 8; i++) {
	map_y[y + i][x] = *addr++;
    }
}

static const int8 neighbors[] = { -1, 32, 1, -32 };

static inline byte should_regrow(byte *ptr) {
    for (byte n = 0; n < SIZE(neighbors); n++) {
	byte food = *(ptr + neighbors[n]) & ~C_DONE;
	if (0 < food && food <= 3) return TRUE;
    }
    return FALSE;
}

static byte **queue;
#define QUEUE(x) *(queue++) = (x)
static inline void regrow_neighbors(byte *ptr) {
    for (byte n = 0; n < SIZE(neighbors); n++) {
	byte *near = ptr + neighbors[n];
	if (*near == 0) {
	    *near = 1;
	    QUEUE(near);
	}
    }
}

static void update_grass(byte cell, byte *ptr) {
    if (cell > 0) {
	regrow_neighbors(ptr);
	if (cell == 3) return;
    }
    else if (!should_regrow(ptr)) {
	return;
    }
    QUEUE(ptr);
    (*ptr)++;
}

static byte migrate(byte cell, byte *ptr) {
    for (byte n = 0; n < SIZE(neighbors); n++) {
	byte *near = ptr + neighbors[n];
	byte food = *near & ~C_DONE;
	if (0 < food && food <= 3) {
	    if (n == 0) cell |= C_FACE;
	    if (n == 2) cell &= ~C_FACE;
	    QUEUE(near);
	    *near |= cell;
	    return TRUE;
	}
    }
    return FALSE;
}

static void update_sheep(byte cell, byte *ptr) {
    byte food = cell & C_FOOD;
    byte size = cell & C_SIZE;
    if (food == 0) {
	cell = size == 4 || migrate(cell - 4, ptr) ? 0 : cell - 4;
    }
    else if (size == C_SIZE) {
	cell -= migrate(4 | (cell & C_FACE), ptr) ? 4 : 1;
    }
    else {
	cell += 3; /* inc size +4, dec food -1 */
    }
    QUEUE(ptr);
    *ptr = cell;
}

static void update_cell(byte *ptr) {
    byte cell = *ptr & (C_FACE | C_SIZE | C_FOOD);

    if (cell & C_SIZE) {
	update_sheep(cell, ptr);
    }
    else {
	update_grass(cell, ptr);
    }
}

static void clean_tags(byte **ptr) {
    while (*ptr) *(*ptr++) &= ~C_DONE;
}

static void advance_cells(byte **ptr) {
    while (*ptr) {
	byte *place = *ptr++;
	if ((*place & (C_DONE | C_PLAY)) == 0) {
	    update_cell(place);
	    *place |= C_DONE;
	}
    }
}

static void advance_forest(byte **ptr) {
    advance_cells(ptr);
    clean_tags(ptr);
}

static void tile_ptr(byte *ptr) {
    byte cell = *ptr;
    if ((cell & C_PLAY) == 0) {
	cell &= (C_FACE | C_SIZE | C_FOOD);
	byte color = cell & C_SIZE ? 7 : 4;
	put_tile(cell, color, ptr - forest);
    }
}

static byte get_face(int8 diff, byte cell) {
    switch (diff) {
    case 1:
	return 0;
    case -1:
	return C_FACE;
    default:
	return cell & C_FACE;
    }
}

static void beep(word p0, word p1, word len) {
    word c0 = 0;
    word c1 = 0;
    __asm__("di");
    for (word i = 0; i < len; i++) {
	out_fe(c0 >= 32768 ? 0x10 : 0x00);
	c0 += p0;
	out_fe(c1 >= 32768 ? 0x10 : 0x00);
	c1 += p1;
    }
    __asm__("ei");
}

static void bite_sound(word distance) {
    word offset = distance << 8;
    beep(1024 - offset, 1024 + offset, 256);
}

static void bite(word dst) {
    for (byte i = 0; i < 4; i++) {
	byte color = i == 3 ? 0x02 : 0x47;
	put_tile(4 + i, color, dst);
	bite_sound(i);
    }
}

static void move_hunter(int8 diff) {
    word dst = pos + diff;
    if ((forest[dst] & C_TILE) == 0) {
	byte cell = forest[pos];
	byte *place = forest + pos;
	sprites = (void *) tiles;
	*place &= C_FOOD;
	tile_ptr(place);
	QUEUE(place);

	byte next = forest[dst];
	sprites = (void *) hunter;
	if (next & C_SIZE) bite(dst);
	byte face = get_face(diff, cell);
	forest[dst] = (next & ~C_FACE) | C_PLAY | face;
	put_tile(face ? 1 : 0, 0x46, dst);
	pos = dst;
    }
}

static byte key_state(void) {
    byte output = ~in_fe(0xfd) & 7;
    output |= (~in_fe(0xfb) & 2) << 2;
    output |= (~in_fe(0x7f) & 1) << 4;
    return output;
}

static byte fast_forward(void) {
    return ~in_fe(0xbf) & 1;
}

static void wait_user_input(void) {
    byte change, prev, next = key_state();
    do {
	prev = next;
	next = key_state();
	change = next & (prev ^ next);
	if (fast_forward()) break;
    } while (change == 0);

    for (byte n = 0; n < SIZE(neighbors); n++) {
	if (change & BIT(n)) {
	    move_hunter(neighbors[n]);
	}
    }
}

static void display_forest(byte **ptr) {
    sprites = (void *) tiles;
    while (*ptr) {
	tile_ptr(*ptr);
	ptr++;
    }
    wait_user_input();
    *queue = 0;
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

static void game_round(byte **src, byte **dst) {
    queue = dst;
    advance_forest(src);
    display_forest(dst);
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

    pos = 200;
    move_hunter(200);

    word i = 0x1234;
    for (;;) {
	put_num(i, 1, 2);
	game_round(update, mirror);
	i = inc10(i);
	put_num(i, 1, 2);
	game_round(mirror, update);
	i = inc10(i);
    }
}

void reset(void) {
    SETUP_STACK();
    setup_system();
    clear_screen();
    precalculate();

    game_loop();
}

typedef signed char int8;
typedef unsigned char byte;
typedef unsigned short word;

#ifdef MSX
static void msx_prefix(void) __naked {
    __asm__(".ascii \"AB\"");
    __asm__(".dw _reset");
    __asm__(".db 0, 0, 0, 0, 0, 0");
}
#endif

#ifdef SMS
static void rom_start(void) __naked {
    __asm__("di");
    __asm__("im 1");
    __asm__("jp _reset");
    __asm__("rom_jmp_end:");
    __asm__(".blkb 0x38 - (rom_jmp_end - _rom_start)");

    __asm__("push af");
    __asm__("in a, (#0xbf)");
    __asm__("bit 7, a");
    __asm__("jp z, no_update");
    __asm__("push bc");
    __asm__("push de");
    __asm__("push hl");

    __asm__("call _vdp_update");

    __asm__("pop hl");
    __asm__("pop de");
    __asm__("pop bc");
    __asm__("no_update:");
    __asm__("pop af");

    __asm__("ei");
    __asm__("ret");
    __asm__("rom_irq_end:");
    __asm__(".blkb 0x66 - (rom_irq_end - _rom_start)");
    __asm__("retn");
}
#endif

#include "data.h"

#define ADDR(obj)	((word) (obj))
#define BYTE(addr)	(* (volatile byte *) (addr))
#define WORD(addr)	(* (volatile word *) (addr))
#define SIZE(array)	(sizeof(array) / sizeof(*(array)))

#ifdef ZXS
#define NOTE(freq)	((word) ((2400.0 * freq) / 440.0))
#define SCALE_HI(n, x)	((n) << (x))
#define SCALE_LO(n, x)	((n) >> (x))
#endif
#ifdef SMS
#define NOTE(freq)	((word) (125000.0 / freq))
#define SCALE_HI(n, x)	((n) >> (x))
#define SCALE_LO(n, x)	((n) << (x))
#endif
#ifdef MSX
#define NOTE(freq)	((word) (freq))
#define SCALE_HI(n, x)	((n) >> (x))
#define SCALE_LO(n, x)	((n) << (x))
#endif

#define POS(x, y)	(((y) << 5) + (x))
#define BIT(n)		(1 << (n))
#define TRUE		1
#define FALSE		0

#define C_BARE		0x0
#define C_FOOD		0x3
#define C_SIZE		0xc
#define C_FACE		BIT(4)
#define C_PLAY		BIT(5)
#define C_DONE		BIT(6)
#define C_TILE		BIT(7)

#define T_SAND		0x80
#define T_ROCK		0x81
#define T_WALL		0x82
#define T_ROLL		0x83
#define T_WAVE		0x84
#define T_LAVA		0x85
#define T_DEER		0x07

#ifdef ZXS
#define SETUP_STACK()	__asm__("ld sp, #0xfdfc")
#define IRQ_BASE	0xfe00
#endif

#ifdef SMS
#define SETUP_STACK()	__asm__("ld sp, #0xdff0")
#endif

#ifdef MSX
#define SETUP_STACK()	__asm__("ld sp, #0x9df0")
#define IRQ_BASE	0x9e00
#endif

static volatile byte vblank;
static byte *map_y[192];

static byte forest[0x2e0];

static byte *update[512];
static byte *mirror[512];

static word pos;
static word epoch;
static word retry;
static byte level;
static byte steps;
static byte wasd;

struct Level { void (*fn)(void); };

void reset(void);

static const byte pixel_map[] = {
    0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01
};

static void __sdcc_call_hl(void) __naked {
    __asm__("jp (hl)");
}

#if defined(ZXS) || defined(MSX)
static void interrupt(void) __naked {
    __asm__("di");
    __asm__("push af");
#ifdef MSX
    __asm__("in a, (#0x99)");
    __asm__("and a");
    __asm__("jp p, irq_done");
#endif
    __asm__("ld a, #1");
    __asm__("ld (_vblank), a");
    __asm__("irq_done: pop af");
    __asm__("ei");
    __asm__("reti");
}

static void setup_irq(byte base) {
    __asm__("di");
    __asm__("ld i, a"); base;
    __asm__("im 2");
    __asm__("ei");
}

static void out_fe(byte data) {
    __asm__("out (#0xfe), a"); data;
}
#endif

static byte in_key(byte a) {
#ifdef ZXS
    __asm__("in a, (#0xfe)");
#endif
#ifdef SMS
    __asm__("in a, (#0xdc)");
#endif
    return a;
}

#ifdef SMS
static void vdp_word(word addr, word data) {
    __asm__("ld c, #0xbf"); addr;
    __asm__("out (c), l");
    __asm__("out (c), h");
    __asm__("ld c, #0xbe"); data;
    __asm__("out (c), e");
    __asm__("out (c), d");
}

static void vdp_init(byte *ptr, byte size) {
    __asm__("ld iy, #2");
    __asm__("add iy, sp");
    __asm__("ld b, (iy)"); size;
    __asm__("ld c, #0xbf");
    __asm__("otir"); ptr;
}

static const byte vdp_registers[] = {
    0x04, 0x80, 0x80, 0x81, 0xff, 0x82, 0xff, 0x83,
    0xff, 0x84, 0xff, 0x85, 0xff, 0x86, 0x0c, 0x87,
    0x00, 0x88, 0x00, 0x89, 0xff, 0x8a
};

static const byte sms_palette[] = {
    0x00, 0x04, 0x08, 0x0c,
    0x06, 0x01, 0x02, 0x03,
    0x1b, 0x10, 0x24, 0x38,
    0x00, 0x15, 0x2a, 0x3f,

    0x00, 0x00, 0x00, 0x00,
    0x0f, 0x01, 0x02, 0x03,
    0x10, 0x24, 0x38, 0x3c,
    0x00, 0x00, 0x00, 0x00,
};

static void vdp_switch(byte value) {
    __asm__("ld c, #0xbf");
    __asm__("out (c), a"); value;
    __asm__("ld a, #0x81");
    __asm__("out (c), a");
}

static byte vdp_state;
static void vdp_enable_display(byte state) {
    if (state != vdp_state) {
	if (state) {
	    vdp_switch(0xe0);
	    __asm__("ei");
	}
	else {
	    __asm__("di");
	    vdp_switch(0x80);
	}
	vdp_state = state;
    }
}

static void vdp_memset(word addr, word count, byte data) {
    __asm__("ld c, #0xbf"); addr;
    __asm__("out (c), l");
    __asm__("out (c), h");
    __asm__("ld c, #0xbe"); data;
    __asm__("push iy");
    __asm__("ld iy, #0x4");
    __asm__("add iy, sp");
    __asm__("ld b, (iy)");
    __asm__("more: out (c), b");
    __asm__("dec de"); count;
    __asm__("ld a, e");
    __asm__("ld l, d");
    __asm__("or a, l");
    __asm__("jp nz, more");
    __asm__("pop iy");
}

static void vdp_transfer(void *ptr, byte size) {
    __asm__("ld iy, #2");
    __asm__("add iy, sp");
    __asm__("ld b, (iy)"); size;
    __asm__("ld c, #0xbe");
    __asm__("otir"); ptr;
}

static void vdp_memcpy(word dst, byte *src, word count) {
    __asm__("ld c, #0xbf"); dst;
    __asm__("out (c), l");
    __asm__("out (c), h");
    while (count > 0xff) {
	vdp_transfer(src, 0);
	count -= 0x100;
	src += 0x100;
    }
    if (count > 0) {
	vdp_transfer(src, count);
    }
}

static word vdp_addr[256];
static word vdp_data[256];
static byte vdp_head, vdp_tail;

static void vdp_put_tile(word n, word tile) {
    if (!vdp_state) {
	vdp_word(0x7800 + (n << 1), tile);
    }
    else {
	while (vdp_head + 1 == vdp_tail) { }
	vdp_addr[vdp_head] = 0x7800 + (n << 1);
	vdp_data[vdp_head] = tile;
	vdp_head++;
    }
}

static void vdp_update(void) {
    vblank = 1;
    byte count = 0;
    word *addr = vdp_addr + vdp_tail;
    word *data = vdp_data + vdp_tail;
    while (vdp_head != vdp_tail) {
	if (count++ > 32) break;
	vdp_word(*addr++, *data++);
	if (++vdp_tail == 0x00) {
	    addr = vdp_addr;
	    data = vdp_data;
	}
    }
}

static void out_7f(byte data) {
    __asm__("out (#0x7f), a"); data;
}

static void sms_psg(byte channel, word frequency, byte volume) {
    channel <<= 5;
    out_7f(0x80 | channel | (frequency & 0xf));
    out_7f(frequency >> 4);
    out_7f(0x90 | channel | (15 - volume));
}

static void sound_off(void) {
    out_7f(0x9f);
    out_7f(0xbf);
}
#endif

#ifdef MSX
static void vdp_ctrl_reg(byte reg, byte val) {
    __asm__("di");
    __asm__("ld c, #0x99");
    __asm__("out (c), l"); val;
    __asm__("or a, #0x80");
    __asm__("out (c), a"); reg;
    __asm__("ei");
}

static void vram_write(word addr, byte val) {
    __asm__("di");
    __asm__("ld c, #0x99");
    __asm__("out (c), l"); addr;
    __asm__("out (c), h"); addr;
    __asm__("ei");
    __asm__("dec c");
    __asm__("out (c), a"); val;
}

static void vdp_memset(word addr, byte data, word count) {
    while (count-- > 0) { vram_write(addr++, data); }
}

static void vdp_memcpy(word addr, byte *ptr, word count) {
    while (count-- > 0) { vram_write(addr++, *ptr++); }
}
#endif

static void memset(byte *ptr, byte data, word len) {
    while (len-- > 0) { *ptr++ = data; }
}

static void memcpy(byte *dst, byte *src, word len) {
    while (len-- > 0) { *dst++ = *src++; }
}

static void setup_system(void) {
#if defined(ZXS) || defined(MSX)
    byte top = (byte) ((IRQ_BASE >> 8) - 1);
    word jmp_addr = (top << 8) | top;
    BYTE(jmp_addr + 0) = 0xc3;
    WORD(jmp_addr + 1) = ADDR(&interrupt);
    memset((byte *) IRQ_BASE, top, 0x101);
    setup_irq(IRQ_BASE >> 8);
#endif
#ifdef SMS
    vdp_init(vdp_registers, SIZE(vdp_registers));
    vdp_memcpy(0xc000, sms_palette, SIZE(sms_palette));
    vdp_memset(0x4000, 0x4000, 0x00);
    vdp_memset(0x7f00, 0x0040, 0xd0);

    vdp_state = FALSE;
    vdp_enable_display(TRUE);
#endif
#ifdef MSX
    vdp_ctrl_reg(0, 0x02);
    vdp_ctrl_reg(1, 0xe0);
    vdp_ctrl_reg(7, 0x00);
#endif
}

static void clear_screen(void) {
#ifdef ZXS
    memset((byte *) 0x5800, 0x00, 0x300);
    memset((byte *) 0x4000, 0x00, 0x1800);
    out_fe(0);
#endif
#ifdef SMS
    vdp_enable_display(FALSE);
    vdp_memset(0x7800, 0x600, 0x00);
    vdp_head = vdp_tail = 0;
    vdp_enable_display(TRUE);
#endif
#ifdef MSX
    vdp_memset(0x6000, 0, 0x1800);
    vdp_memset(0x4000, 0, 0x1b00);
#endif
}

static void precalculate(void) {
#ifdef ZXS
    for (byte y = 0; y < 192; y++) {
	byte f = ((y & 7) << 3) | ((y >> 3) & 7) | (y & 0xc0);
	map_y[y] = (byte *) (0x4000 + (f << 5));
    }
#endif
}

static void put_char(char symbol, word n, byte color) {
#ifdef ZXS
    byte x = n & 0x1f;
    byte y = (n >> 2) & ~7;
    BYTE(0x5800 + n) = color;
    byte *addr = (byte *) 0x3c00 + (symbol << 3);
    byte *ptr = map_y[y] + x;
    for (byte i = 0; i < 8; i++) {
	*ptr = *addr++;
	ptr += 0x100;
    }
#endif

#ifdef SMS
    vdp_put_tile(n, 0xe0 + symbol);
    color;
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

static word add10(word a, word b) __naked {
    __asm__("ld a, l"); a;
    __asm__("add e"); b;
    __asm__("daa");
    __asm__("ld e, a");
    __asm__("ld a, h");
    __asm__("adc a, d");
    __asm__("daa");
    __asm__("ld d, a");
    __asm__("ret");
}

static word sub10(word a, word b) __naked {
    __asm__("ld a, l"); a;
    __asm__("sub e"); b;
    __asm__("daa");
    __asm__("ld e, a");
    __asm__("ld a, h");
    __asm__("sbc a, d");
    __asm__("daa");
    __asm__("ld d, a");
    __asm__("ret");
}

static void put_tile(byte cell, word n) {
#ifdef ZXS
    byte x = n & 0x1f;
    byte y = (n >> 2) & ~7;
    BYTE(0x5800 + n) = tiles_color[cell];
    const byte *addr = tiles + (cell << 3);
    byte *ptr = map_y[y] + x;
    for (byte i = 0; i < 8; i++) {
	*ptr = *addr++;
	ptr += 0x100;
    }
#endif

#ifdef SMS
    vdp_put_tile(n, cell);
#endif
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
	if ((*place & (C_TILE | C_DONE | C_PLAY)) == 0) {
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
    if ((cell & (C_TILE | C_PLAY)) == 0) {
	cell &= (C_FACE | C_SIZE | C_FOOD);
	put_tile(cell, ptr - forest);
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
#ifdef ZXS
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
    out_fe(0x00);
#endif

#ifdef SMS
    if (p0) sms_psg(0, p0, 15);
    if (p1) sms_psg(1, p1, 15);
    len = (len << 3) - (len >> 1);
    for (word i = 0; i < len; i++) { }
    sound_off();
#endif
}

static void bite_sound(word distance) {
    word offset = 3 * NOTE(187.8) / 2;
    offset = SCALE_LO(offset, 4 - distance);
    beep(NOTE(187.8) - offset, NOTE(187.8) + offset, 256);
}

static void rolling_rock_sound(void) {
    for (byte i = 0; i < 4; i++) {
	byte offset = 0x10 << i;
	beep(768 - (offset << 1), 768 + offset, 64);
    }
}

static word meat;
static void bite(word dst) {
    meat = add10(meat, 5);
    for (byte i = 0; i < 4; i++) {
	put_tile(36 + i, dst);
	bite_sound(i);
    }
}

static byte rock_type(byte pos) {
    return 34 + ((pos ^ (pos >> 5)) & 1);
}

static byte roll_rock(int8 diff) {
    word dst = pos + (diff << 1);
    if ((forest[dst] & (C_TILE | C_SIZE)) == 0) {
	forest[dst] = T_ROCK;
	goto success;
    }
    if (forest[dst] == T_SAND) {
	forest[dst] = T_ROLL;
	goto success;
    }
    return FALSE;
  success:
    rolling_rock_sound();
    put_tile(rock_type(dst), dst);
    return TRUE;
}

#define IS_ROCK(cell) (((cell) & ~2) == T_ROCK)

static byte can_move_into(byte next, int8 diff) {
    return next <= T_SAND || (IS_ROCK(next) && roll_rock(diff));
}

static byte is_grazer(word dst) {
    byte cell = forest[dst] & (C_SIZE | C_TILE);
    return cell > 0 && cell < C_TILE;
}

static void put_sprite(byte cell, byte base, word n);

static void put_sand(word n) {
    forest[n] = T_SAND;
    put_sprite(6, 0, n);
}

static byte standing;
static void leave_tile(byte *place) {
    if (standing < C_TILE || standing == T_ROCK) {
	*place = C_BARE;
	tile_ptr(place);
    }
    else {
	put_sand(place - forest);
    }
}

static void move_hunter(int8 diff) {
    word dst = pos + diff;
    if (dst < SIZE(forest) && can_move_into(forest[dst], diff)) {
	byte *place = forest + pos;
	byte cell = *place;
	leave_tile(place);
	QUEUE(place);

	standing = forest[dst];
	if (is_grazer(dst)) bite(dst);
	byte face = get_face(diff, cell);
	forest[dst] = C_PLAY | face;
	put_tile(face ? 33 : 32, dst);
	pos = dst;
    }
}

static void put_hunter(word where) {
    pos = where;
    forest[pos] = 0;
    standing = C_BARE;
    move_hunter(0);
}

static void put_item(word where, byte type, byte sprite) {
    forest[where] = type;
    put_tile(sprite, where);
}

static void queue_item(word where, byte type, byte sprite) {
    put_item(where, type, sprite);
    QUEUE(forest + where);
}

static byte fast_forward(void) {
#if ZXS
    return ((~in_key(0xbf) & 1) << 1) | ((~in_key(0x7f) & 8) >> 2);
#endif

#if SMS
    return (~in_key(0) & 0x20) >> 4;
#endif
}

static byte skip_epoch(void) {
#if ZXS
    byte reg = ~in_key(0x7f);
    return ((reg & 1) << 4) | ((reg & 4) << 2);
#endif

#if SMS
    return ~in_key(0) & 0x10;
#endif
}

static byte space_or_enter(void) {
    return skip_epoch() | fast_forward();
}

static byte wait_space_or_enter(byte (*callback)(void)) {
    byte prev, next = space_or_enter();
    do {
	prev = next;
	next = space_or_enter();
	if (callback != 0 && vblank == 1) {
	    vblank = 0;
	    if (callback()) {
		return TRUE;
	    }
	}
    } while ((next & (prev ^ next)) == 0);
    return FALSE;
}

static byte movement_keys(void) {
#ifdef ZXS
    byte output;
    if (wasd) {
	output = in_key(0xfd) & 7;
	output |= (in_key(0xfb) & 2) << 2;
    }
    else {
	output = in_key(0xdf);
	output = ((output & 1) << 2) | ((output & 2) >> 1);
	output |= (in_key(0xfb) & 1) << 3;
	output |= (in_key(0xfd) & 1) << 1;
    }
    return (~output) & 0xf;
#endif

#ifdef SMS
    byte output = ~in_key(0);
    return (output & 0x02)
	| ((output & 0x04) >> 2)
	| ((output & 0x08) >> 1)
	| ((output & 0x01) << 3);
#endif
}

static byte key_state(void) {
    return skip_epoch() | movement_keys();
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
    while (*ptr) {
	tile_ptr(*ptr);
	ptr++;
    }
}

static byte flip_bits(byte source) {
    byte result = 0;
    for (byte i = 0; i < 8; i++) {
	result = result << 1;
	result |= source & 1;
	source = source >> 1;
    }
    return result;
}

#if defined(ZXS)
static const byte *sprite;
static const byte *sprite_color;
#define TILE_ATTRIBURE(x)
#define TILESET(tiles, color, offset) \
    sprite_color = color; \
    sprite = tiles;

#elif defined(MSX)
#define TILE_ATTRIBURE(x)
#define TILESET(tiles, color, offset)

#elif defined(SMS)
static word sprite_offset;
#define TILE_ATTRIBURE(x) \
    sprite_offset |= (x);
#define TILESET(tiles, color, offset) \
    vdp_enable_display(FALSE); \
    vdp_memcpy(0x4000 + (offset << 5), tiles, SIZE(tiles)); \
    vdp_enable_display(TRUE); \
    sprite_offset = offset;
#endif

static void put_sprite(byte cell, byte base, word n) {
    byte index = base + (cell & 0x1f);

#ifdef ZXS
    byte x = n & 0x1f;
    byte y = (n >> 2) & ~7;
    byte flipH = cell & 0x20;
    byte flipV = cell & 0x40;
    BYTE(0x5800 + n) = sprite_color[index];
    const byte *addr = sprite + (index << 3);
    if (flipV) addr = addr + 7;
    byte *ptr = map_y[y] + x;
    for (byte i = 0; i < 8; i++) {
	byte data = *addr;
	if (flipV) addr--; else addr++;
	if (flipH) data = flip_bits(data);
	*ptr = data;
	ptr += 0x100;
    }
#endif

#ifdef SMS
    word id = sprite_offset + index;
    byte *ptr = (byte *) &id;
    ptr[1] |= (cell & 0x60) >> 4;
    vdp_put_tile(n, id);
#endif
}

static void special_cell(byte cell, word n) {
    switch (cell) {
    case 0:
	forest[n] = 0;
	break;
    case 1:
	put_hunter(n);
	break;
    case 2:
	put_item(n, C_FOOD, C_FOOD);
	break;
    case 3:
	queue_item(n, T_DEER, T_DEER);
	break;
    case 4:
    case 5:
	byte id = cell == 4 ? T_ROCK : T_ROLL;
	queue_item(n, id, rock_type(n));
	break;
    case 6:
	put_sand(n);
	break;
    }
}

static byte in_game;
static void display_cell(byte cell, byte base, word n) {
    if (in_game && cell < 7) {
	special_cell(cell, n);
    }
    else if (cell + base > 0) {
	forest[n] = T_WALL;
	put_sprite(cell, base, n);
    }
}

static void raw_image(byte *level, byte game, word size, word n) {
    byte base = 0;
    in_game = game;
    for (word i = 0; i < size; i++) {
	byte cell = level[i];
	switch (cell & 0xc0) {
	case 0x80:
	    byte count = cell & 0x7f;
	    byte repeat = level[++i];
	    for (byte j = 0; j < count; j++) {
		display_cell(repeat, base, n++);
	    }
	    break;
	case 0xc0:
	    base = (cell & 0x3f) << 2;
	    break;
	default:
	    display_cell(cell, base, n++);
	    break;
	}
    }
}

static void display_image(byte *level, byte game, word size, word n) {
#ifdef SMS
    vdp_enable_display(FALSE);
#endif
    raw_image(level, game, size, n);
#ifdef SMS
    vdp_enable_display(TRUE);
#endif
}

static void increment_epoch(void) {
    put_num(epoch, POS(7, 23), 5);
    epoch = add10(epoch, 1);
    steps++;
}

static int8 (*finish)(void);

static int8 game_round(byte **src, byte **dst) {
    queue = dst;
    advance_forest(src);
    display_forest(dst);
    int8 ret = finish();
    increment_epoch();
    if (ret == 0) {
	wait_user_input();
    }
    QUEUE(0);
    return ret;
}

static void reset_memory(void) {
#ifdef SMS
    TILESET(tiles, 0, 0);
    TILESET(font, 0, 0x100);
#endif

    memset(update, 0x00, sizeof(update));
    memset(mirror, 0x00, sizeof(mirror));
    memset(forest, 0x00, sizeof(forest));
    level = 0;
    retry = 0;
    wasd = 0;
}

static byte no_grazers(void) {
    for (word i = 0; i < SIZE(forest); i++) {
	byte cell = forest[i];
	if (C_FOOD < cell && cell < C_PLAY) {
	    return 0;
	}
    }
    return 1;
}

static int8 ending_400(void) {
    if (epoch >= 0x400) {
	return 1;
    }
    else if (no_grazers()) {
	return -1;
    }
    return 0;
}

static byte no_empty_spaces(void) {
    for (word i = 0; i < SIZE(forest); i++) {
	if (forest[i] == 0) return 0;
    }
    return 1;
}

static byte no_vegetation(void) {
    for (word i = 0; i < SIZE(forest); i++) {
	byte cell = forest[i];
	if (cell > 0 && cell <= 3) return 0;
    }
    return 1;
}

static int8 ending_vegetation(void) {
    if (no_grazers()) {
	if (no_empty_spaces()) {
	    return 1;
	}
	else if (no_vegetation()) {
	    return -1;
	}
    }
    return 0;
}

static int8 ending_no_weeds(void) {
    if (no_vegetation()) {
	return 1;
    }
    if (no_grazers()) {
	return -1;
    }
    return 0;
}

static int8 ending_escape(void) {
    byte cell = forest[POS(5, 0)];
    if ((cell & C_PLAY) || cell == T_ROCK) {
	return 1;
    }
    else if (cell & C_SIZE) {
	return -1;
    }
    return 0;
}

static void put_wave(word n, byte tile, byte color) {
    forest[n] = T_WAVE;

#ifdef ZXS
    put_sprite(tile, 0, n);
    BYTE(0x5800 + n) = color ? 0x05 : 0x01;
#endif

#ifdef SMS
    word id = 40 + tile;
    if (color) id |= 0x800;
    vdp_put_tile(n, id);
#endif
}

static byte tsunami_rnd;
static void draw_wave(int8 len, byte color) {
    byte x = len < 0 ? -len : 0;
    byte y = len >= 0 ? len : 0;
    for (word n = (y << 5) + x; x < 32 && y < 23; x++, y++, n += 33) {
	if (forest[n] != T_WALL) {
	    put_wave(n, color ? 8 : 7, color);
	}
    }
}

static void recede_wave(int8 len) {
    byte x = len < 0 ? -len : 0;
    byte y = len >= 0 ? len : 0;
    for (word n = (y << 5) + x; x < 32 && y < 23; x++, y++, n += 33) {
	if (forest[n] == T_WALL) {
	    continue;
	}
	byte sum = x + y;
	if (x == 0 || y == 22 || sum < 18 || sum > 36) {
	    continue;
	}
	if (tsunami_rnd++ == 11) {
	    tsunami_rnd = 0;
	    put_sand(n);
	    continue;
	}
	queue_item(n, C_BARE, C_BARE);
    }
}

static int8 wave_len, wave_dir;
static int8 ending_tsunami(void) {
    if (epoch & 1) {
	if (wave_dir < 0) {
	    draw_wave(wave_len, 1);
	}
	else {
	    recede_wave(wave_len);
	}
	if (wave_len == -24 && wave_dir == -1) {
	    put_sand(POS(30, 7));
	    wave_dir = 1;
	}
	else {
	    wave_len += wave_dir;
	}
    }
    else if (wave_dir < 0) {
	draw_wave(wave_len + 2, 0);
    }
    if (forest[pos] == T_WAVE || no_grazers()) {
	return -1;
    }
    if (wave_len == 24 && wave_dir == 1) {
	return 1;
    }
    else {
	return 0;
    }
}

static word last_pos, stayed;
static int8 ending_equilibrium(void) {
    if (++stayed == 800) {
	return 1;
    }
    if (last_pos != pos) {
	last_pos = pos;
	stayed = 0;
    }
    if (no_grazers()) {
	return -1;
    }
    return 0;
}

static int8 tide_pos[24];
static const int8 tide_max[24] = {
    10, 21, 11, 20, 11, 20, 12, 19, 12, 19, 13, 18,
    13, 18, 13, 18, 13, 18, 12, 19, 12, 19, 11, 20,
};

static int8 tidal_put(int8 *ptr, int8 y, int8 dir) {
    word n = (y << 5) + *ptr + dir;
    if (forest[n] < C_TILE) {
	(*ptr) += dir;
	put_wave(n, 7, 1);
	return 1;
    }
    return 0;
}

static int8 recede_put(int8 *ptr, int8 y, int8 dir) {
    word n = (y << 5) + *ptr;
    if (forest[n] == T_WAVE) {
	(*ptr) += dir;
	byte *ptr = forest + n;
	put_item(n, C_BARE, C_BARE);
	if (should_regrow(ptr)) {
	    QUEUE(ptr);
	}
	return 1;
    }
    return 0;
}

static void tidal_movement(void) {
  restart_tide:
    byte advance = 0;
    int8 *ptr = tide_pos;
    for (int8 y = 7; y < 19; y++) {
	if (wave_dir == 0) {
	    advance += tidal_put(ptr++, y, 1);
	    advance += tidal_put(ptr++, y, -1);
	}
	else if (wave_dir == 1) {
	    advance += recede_put(ptr++, y, -1);
	    advance += recede_put(ptr++, y, 1);
	}
    }
    if (advance == 0) {
	wave_dir = (wave_dir + 1) & 3;
	if (wave_dir == 1) {
	    goto restart_tide;
	}
    }
}

static int8 ending_migration(void) {
    tidal_movement();

    byte cell = forest[POS(8, 22)];
    if (cell & C_SIZE) {
	return 1;
    }
    else if (no_grazers() || forest[pos] == T_WAVE) {
	return -1;
    }
    return 0;
}

static byte half_grazers(void) {
    byte alive = 0;
    for (word i = 0; i < SIZE(forest); i++) {
	byte cell = forest[i];
	if (C_FOOD < cell && cell < C_PLAY) {
	    alive |= ((i & 0x1f) > 0x10) ? 1 : 2;
	}
	if (alive == 3) return 0;
    }
    return 1;
}

static int8 ending_aridness(void) {
    if (epoch >= 0x400) {
	return 1;
    }
    else if (half_grazers()) {
	return -1;
    }
    return 0;
}

static const int8 around[] = {
    1, 33, 32, 31, -1, -33, -32, -31
};

static int8 hunter_on_sand(byte cell) {
    return (cell & C_PLAY) && standing == T_SAND;
}

static byte is_dryable(word next) {
    byte cell = forest[next];
    return cell < C_TILE && !hunter_on_sand(cell);
}

static word drying;
static int8 drying_dir;
static void circular_drying(void) {
  repeat:
    for (int8 i = -1; i <= 1; i++) {
	int8 dir = (i + drying_dir) & 7;
	word next = drying + around[dir];
	if (is_dryable(next)) {
	    drying_dir = dir;
	    drying = next;
	    return;
	}
    }
    drying_dir++;
    drying_dir &= 7;
    goto repeat;
}

static void advance_drying(void) {
    switch (drying) {
    case POS(18, 12):
	drying -= 1;
	break;
    case POS(8, 16):
    case POS(7, 15):
	drying -= 32;
	break;
    case POS(8, 7):
    case POS(8, 8):
    case POS(10, 7):
    case POS(13, 8):
    case POS(14, 9):
    case POS(15, 10):
    case POS(17, 12):
	drying += 32;
	break;
    default:
	circular_drying();
    }
}

static int8 ending_lonesome(void) {
    byte *place = forest + drying;
    if (*place <= C_FOOD) {
	put_sand(drying);
	advance_drying();
    }
    else if (*place & C_PLAY) {
	standing = T_SAND;
	advance_drying();
    }
    if (no_grazers()) {
	return -1;
    }
    if (drying == POS(20, 10)) {
	return 1;
    }
    return 0;
}

static void put_lava(word n) {
    put_sprite(10, 0, n);
    forest[n] = T_LAVA;
    QUEUE(forest + n);
}

static void lava_flow(byte *ptr) {
    for (byte i = 0; i < SIZE(neighbors); i++) {
	byte *near = ptr + neighbors[i];
	if (*near < C_TILE || *near == T_SAND) {
	    put_lava(near - forest);
	}
    }
}

static void advance_lava(void) {
    byte count = steps & 7;
    byte **ptr = queue < mirror ? mirror : update;
    while (*ptr) {
	byte *place = *ptr++;
	if (*place == T_LAVA) {
	    if (count == 0) {
		lava_flow(place);
	    }
	    else {
		QUEUE(place);
	    }
	}
    }
}

static int8 ending_eruption(void) {
    advance_lava();
    if (no_grazers() || forest[pos] == T_LAVA) {
	return -1;
    }
    if (epoch == 0x300) {
	return 1;
    }
    return 0;
}

static const word seeding[] = {
    POS(8, 4), POS(9, 5), POS(10, 4),
    POS(19, 3), POS(20, 4), POS(21, 3),
    POS(14, 11), POS(15, 12), POS(16, 11),
};

static int8 ending_fertility(void) {
    for (byte i = 0; i < SIZE(seeding); i++) {
	word n = seeding[i];
	if (forest[n] == 0) {
	    put_item(n, 1, 1);
	}
    }
    if (no_grazers()) {
	return 1;
    }
    return 0;
}

static int8 ending_erosion(void) {
    put_str("MEAT:", POS(12, 23), 5);
    put_num(meat, POS(17, 23), 5);
    if (meat >= 0x200) {
	return  1;
    }
    if (meat == 0) {
	return -1;
    }
    meat = sub10(meat, 1);
    if (no_grazers()) {
	return -1;
    }
    return 0;
}

static void adat_meitas(void);

static void finish_game(void) {
    clear_screen();

    TILESET(sunset, sunset_color, 0);
    TILE_ATTRIBURE(0x800);
    memset(forest, 0, SIZE(forest));
    display_image(sunset_map, 0, SIZE(sunset_map), 0);

    adat_meitas();
    reset();
}

static void use_fence_sprites(void) {
    TILESET(fence, fence_color, 40);
}

static void fenced_level(byte *level, word size) {
    clear_screen();
    use_fence_sprites();
    display_image(level, 1, size, 0);
}

static void quarantine_level(void) {
    put_str("- QUARANTINE -", POS(9, 4), 0x44);
    put_str("Prevent GRAZER population", POS(3, 16), 4);
    put_str("from collapse til EPOCH 400", POS(2, 17), 4);
    wait_space_or_enter(0);

    fenced_level(quarantine_map, SIZE(quarantine_map));

    finish = &ending_400;
}

static void earthquake_level(void) {
    put_str("- EARTHQUAKE -", POS(9, 4), 0x44);
    put_str("Prevent GRAZERs from escaping", POS(2, 16), 4);
    wait_space_or_enter(0);

    fenced_level(earthquake_map, SIZE(earthquake_map));
    QUEUE(forest + POS(5, 1));

    finish = &ending_escape;
}

static void gardener_level(void) {
    put_str("- PREDATOR -", POS(10, 4), 0x44);
    put_str("Hunt down invasive GRAZER", POS(4, 16), 4);
    put_str("species so that vegetation", POS(3, 17), 4);
    put_str("can fully recover and regrow", POS(2, 18), 4);
    wait_space_or_enter(0);

    fenced_level(gardener_map, SIZE(gardener_map));

    finish = &ending_vegetation;
}

static void flooding_level(void) {
    put_str("- FLOODING -", POS(10, 4), 0x44);
    put_str("Recent FLOODING had caused ", POS(2, 16), 4);
    put_str("spread of weeds that needs", POS(2, 17), 4);
    put_str("to be eliminated completely", POS(2, 18), 4);
    put_str("Some GRAZERs must remain", POS(3, 20), 4);
    wait_space_or_enter(0);

    fenced_level(flooding_map, SIZE(flooding_map));

    finish = &ending_no_weeds;
}

static void tsunami_level(void) {
    tsunami_rnd = 11;
    wave_len = 24;
    wave_dir = -1;

    put_str("- TSUNAMI -", POS(10, 4), 0x44);
    put_str("Help GRAZERs survive TSUNAMI", POS(2, 16), 4);
    wait_space_or_enter(0);

    fenced_level(tsunami_map, SIZE(tsunami_map));

    finish = &ending_tsunami;
}

static byte check_R(void) {
    return movement_keys() & 1;
}

static void load_level(byte n);
static void equilibrium_level(void) {
    stayed = 0;
    last_pos = 0;
    put_str("- EQUILIBRIUM -", POS(8, 4), 0x44);
    put_str("Reach EQUILIBRIUM so that you", POS(1, 16), 4);
    put_str("stay on the same spot for 800", POS(1, 17), 4);
    put_str("EPOCHs and GRAZERs survive", POS(2, 18), 4);
    if (retry >= 3) {
	put_str("LEFT - load previous level", POS(3, 20), 4);
	if (wait_space_or_enter(&check_R)) {
	    load_level(--level);
	    return;
	}
    }
    else {
	wait_space_or_enter(0);
    }

    fenced_level(equilibrium_map, SIZE(equilibrium_map));

    finish = &ending_equilibrium;
}

static void migration_level(void) {
    wave_dir = 3;
    memcpy(tide_pos, tide_max, sizeof(tide_max));

    put_str("- MIGRATION -", POS(9, 4), 0x44);
    put_str("Help GRAZERs migrate south", POS(3, 16), 4);
    wait_space_or_enter(0);

    fenced_level(migration_map, SIZE(migration_map));

    finish = &ending_migration;
}

static void aridness_level(void) {
    put_str("- DROUGHT -", POS(10, 4), 0x44);

    put_str("Both GRAZER subpopulations", POS(2, 16), 4);
    put_str("must survive for 400 EPOCHs", POS(2, 17), 4);
    wait_space_or_enter(0);

    fenced_level(aridness_map, SIZE(aridness_map));

    finish = &ending_aridness;
}

static void lonesome_level(void) {
    drying_dir = 0;
    drying = POS(19, 6);
    put_str("- EXTINCTION -", POS(9, 4), 0x44);

    put_str("Make sure last inhabitable", POS(3, 16), 4);
    put_str("four spots is occupied by", POS(3, 17), 4);
    put_str("lonesome GRAZERs", POS(7, 18), 4);
    wait_space_or_enter(0);

    fenced_level(lonesome_map, SIZE(lonesome_map));

    finish = &ending_lonesome;
}

static void eruption_level(void) {
    put_str("- ERUPTION -", POS(10, 4), 0x44);

    put_str("Help GRAZERs survive ERUPTION", POS(2, 16), 4);
    put_str("until EPOCH 300", POS(8, 17), 4);
    wait_space_or_enter(0);

    fenced_level(eruption_map, SIZE(eruption_map));

    TILESET(volcano, volcano_color, 72);
    TILE_ATTRIBURE(0x800);
    display_image(volcano_map, 0, SIZE(volcano_map), 0xc0);

    use_fence_sprites();
    put_lava(POS(15, 6));

    finish = &ending_eruption;
}

static void fertility_level(void) {
    put_str("- FERTILITY -", POS(9, 4), 0x44);

    put_str("Due to the parasitic outbreak", POS(1, 16), 4);
    put_str("all GRAZERs must be eliminated", POS(1, 17), 4);
    wait_space_or_enter(0);

    fenced_level(fertility_map, SIZE(fertility_map));

    finish = &ending_fertility;
}

static void erosion_level(void) {
    meat = 0x50;

    put_str("- EROSION -", POS(10, 4), 0x44);
    put_str("Stock up 200 pieces of MEAT", POS(2, 16), 4);
    put_str("Don't starve!", POS(10, 18), 4);
    wait_space_or_enter(0);

    fenced_level(erosion_map, SIZE(erosion_map));

    finish = &ending_erosion;
}

static const struct Level all_levels[] = {
    { &gardener_level },
    { &quarantine_level },
    { &earthquake_level },
    { &tsunami_level },
    { &flooding_level },
    { &equilibrium_level },
    { &migration_level },
    { &aridness_level },
    { &lonesome_level },
    { &eruption_level },
    { &fertility_level },
    { &erosion_level },
    { &finish_game },
};

static void load_level(byte n) {
    clear_screen();
    all_levels[n].fn();
}

static void init_variables(void) {
    epoch = 0;
    steps = 0;
    queue = update;
    load_level(level);
    put_str("EPOCH:0000", POS(1, 23), 5);
}

const word wah_wah[] = { // D4 -> C4# -> C4 -> B3
    NOTE(293.7), NOTE(277.1), NOTE(261.6), NOTE(246.9),
};

static void sad_trombone_wah_wah_wah(void) {
    for (byte i = 0; i < SIZE(wah_wah); i++) {
	word period = wah_wah[i];
	if (i > 0) beep(0, 0, 500);
	for (byte j = 0; j < (i == 3 ? 20 : 10); j++) {
	    beep(SCALE_LO(period, j & 1), SCALE_HI(period, j & 3), 150);
	}
    }
}

static void success_tune(void) {
    static const word tune[] = {
	NOTE(130.8), NOTE(164.8), 0,
	NOTE(196.0), NOTE(164.8), 0,
	NOTE(196.0),
    };
    static const byte delay[] = {
	2, 1, 1, 2, 1, 1, 2
    };
    for (byte i = 0; i < SIZE(tune); i++) {
	for (byte n = 0; n < 4; n++) {
	    word period = SCALE_HI(tune[i], n);
	    beep(period, period, delay[i] << 8);
	}
    }
    for (byte i = 0; i < 4; i++) {
	beep(SCALE_HI(NOTE(196.0), 2), SCALE_HI(NOTE(196.0), 2), 256);
	beep(SCALE_HI(NOTE(196.0), 3), SCALE_HI(NOTE(196.0), 3), 256);
    }
}

#define G3  NOTE(196.0)
#define D4  NOTE(293.7)
#define E4  NOTE(329.6)
#define F4s NOTE(369.9)
#define G4  NOTE(392.0)
#define A4  NOTE(440.0)
#define B4  NOTE(493.9)
#define C5  NOTE(523.4)
#define D5  NOTE(587.3)
#define E5  NOTE(659.2)
#define F5s NOTE(740.0)
#define G5  NOTE(784.0)
#define A5  NOTE(880.0)
#define PP  0

#define L2  40
#define L4  20
#define L4t 20 | 0x8000
#define L8  10
#define L8t 10 | 0x8000

static const word music1[] = {
    B4, L4t, D4, L4, D4, L4,  D4, L4, G4, L4t, F4s, L4, G4, L4,  A4, L4,
    B4, L4t, D4, L4, D4, L4,  D4, L4, G4, L4t, F4s, L4, G4, L4,  A4, L4,
    B4, L4,  B4, L4, C5, L4t, B4, L4, B4, L4,  A4,  L4, A4, L2,
    A4, L4,  A4, L4, B4, L4t, A4, L4, A4, L4,  G4,  L4, G4, L2,
    0, 0
};

static const word music2[] = {
    G3, L2, G3, L2, G3, L2, G3, L2,
    G3, L2, G3, L2, G3, L2, G3, L2,
    D4, L2, D4, L2, D4, L2, D4, L2,
    D4, L2, D4, L2, G3, L2, G3, L2,
    0, 0,
};

struct Channel {
    const word *base;
    const word *tune;
    byte duration;
    word period;
    byte silent;
    byte decay;
    byte num;
};

static byte melody;
static void next_note(struct Channel *channel) {
    const word *tune = channel->tune;
    if (tune[1] == 0) {
	tune = channel->base;
	channel->tune = tune;
	if (channel->num == 0) {
	    melody++;
	}
    }

    word length = tune[1];
    if (length & 0x8000) {
	channel->decay = length & 0xff;
	channel->silent = 0;
    }
    else {
	byte half = length >> 1;
	channel->decay = half;
	channel->silent = half;
    }
    channel->period = tune[0];
}

static void init_channel(struct Channel *channel, const word *base) {
    channel->base = base;
    channel->tune = base;
    next_note(channel);
}

static byte pause;
static void update_pause(struct Channel *channel) {
    pause = 0xff;
    for (byte i = 0; i < 2; i++) {
	byte decay = channel[i].decay;
	byte silent = channel[i].silent;
	if (decay > 0 && decay <= pause) {
	    pause = decay;
	}
	else if (decay == 0 && silent < pause) {
	    pause = silent;
	}
    }
}

static void advance_channel(struct Channel *channel) {
    if (channel->decay > 0) {
	channel->decay -= pause;
	if (channel->decay == 0) {
	    channel->period = 0;
	}
    }
    else if (channel->silent == 0) {
	channel->tune += 2;
	next_note(channel);
    }
    else {
	channel->silent -= pause;
    }
}

static void adat_meitas(void) {
    const word *base[] = { music1, music2 };
    struct Channel channels[SIZE(base)];

    for (byte i = 0; i < SIZE(base); i++) {
	channels[i].num = i;
	init_channel(channels + i, base[i]);
    }

    melody = 0;
    while (!space_or_enter() && melody < 2) {
	update_pause(channels);

	word p0 = channels[0].period;
	word p1 = channels[1].period;
	beep(p0, p1, pause << 7);
	vblank = 1;

	for (byte i = 0; i < SIZE(base); i++) {
	    advance_channel(channels + i);
	}
    }
    wait_space_or_enter(0);
}

static void display_msg(const char *text_message) {
    raw_image(dialog_map, 0, SIZE(dialog_map), 0x140);
    put_str(text_message, POS(12, 11), 5);
}

static void game_loop(void) {
    init_variables();

    int8 ending;

    do {
	ending = game_round(update, mirror);
	if (ending) break;
	ending = game_round(mirror, update);
    } while (!ending);

    switch (ending) {
    case  1:
	display_msg("  DONE  ");
	success_tune();
	retry = 0;
	level++;
	break;
    case -1:
	display_msg(" FAILED ");
	sad_trombone_wah_wah_wah();
	retry++;
	break;
    }

    wait_space_or_enter(0);
}

static void grass_stripe(word n, byte len) {
    for (byte i = 0; i < len; i++) {
	byte cell = 3;
	byte j = len - i;
	if (i == 0) cell = 1;
	if (j == 1) cell = 1 | C_FACE;
	if (i == 1 | j == 2) cell = 2;
	put_tile(cell, n++);
    }
}

#ifdef ZXS
static void title_flash(byte offset, byte color) {
    word addr = 0x5900;
    for (word i = 0; i < 5; i++) {
	if (offset < 0x20) {
	    BYTE(addr + offset) = color;
	}
	addr += 0x20;
	offset++;
    }
}
#endif

static byte eat;
static void animate_title(void) {
    byte frame = (eat & 0x3f) < 32;
    put_tile(frame ? 0x1e : 0x1f, POS(12, 3));
    put_tile(frame ? 0x1b : 0x1a, POS(22, 21));
    byte feed = (frame ? 0x0e : 0x0f);
    put_tile(eat > 192 ? 0xc : feed, POS( 7, 4));
#ifdef ZXS
    byte roll = (eat >> 1) & 0x3f;
    title_flash(roll - 0x10, 0x04);
    title_flash(roll - 0x08, 0x44);
#endif
    eat++;
}

#ifdef ZXS
static byte read_1_or_2(void) {
    return ~in_key(0xf7) & 3;
}

static void wait_start(void) {
    byte prev, next = read_1_or_2();
    do {
	prev = next;
	next = read_1_or_2();
	if (vblank == 1) {
	    vblank = 0;
	    animate_title();
	}
    } while ((next & (prev ^ next)) == 0);
    wasd = next & 2;
}
#endif

#ifdef SMS
static void wait_start(void) {
    do {
	if (vblank) {
	    animate_title();
	    vblank = 0;
	}
    } while ((in_key(0) & 0x30) == 0x30);
}
#endif

#ifdef MSX
static void wait_start(void) {
}
#endif

static void title_screen(void) {
    clear_screen();
    TILESET(logo, mirror, 72);
    memset(mirror, 0, sizeof(logo) / 8);
    display_image(logo_map, 0, SIZE(logo_map), 0x100);

#ifdef ZXS
    put_str("ENTER or N to fast forward", POS(3, 15), 5);
    put_str("SPACE or M skip one epoch", POS(3, 16), 5);
    put_str("1 - QAOP keys", POS(9, 18), 5);
    put_str("2 - WASD keys", POS(9, 19), 5);
#endif

#ifdef SMS
    put_str("Press START", POS(10, 17), 5);
#endif

    grass_stripe(POS( 6, 2), 11);
    grass_stripe(POS( 3, 3), 9);
    grass_stripe(POS(13, 3), 6);
    grass_stripe(POS( 1, 4), 6);
    grass_stripe(POS( 8, 4), 8);
    grass_stripe(POS( 3, 5), 11);

    grass_stripe(POS(14, 21), 8);
    grass_stripe(POS(23, 21), 6);

    eat = 0;
    wait_start();
}

void reset(void) {
    SETUP_STACK();
    setup_system();
    precalculate();
    reset_memory();
    title_screen();

    for (;;) game_loop();
}

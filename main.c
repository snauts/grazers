typedef signed char int8;
typedef unsigned char byte;
typedef unsigned short word;

#include "data.h"

#define ADDR(obj)	((word) (obj))
#define BYTE(addr)	(* (volatile byte *) (addr))
#define WORD(addr)	(* (volatile word *) (addr))
#define SIZE(array)	(sizeof(array) / sizeof(*(array)))
#define NOTE(freq)	((2400.0 * freq) / 440.0)

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

#define T_ROCK		0x80

#ifdef ZXS
#define is_vsync()	vblank
#define SETUP_STACK()	__asm__("ld sp, #0xfdfc")
#define IRQ_BASE	0xfe00
#endif

static volatile byte vblank;
static byte *map_y[192];

static byte forest[0x2e0];

static byte *update[512];
static byte *mirror[512];

static word pos;
static word epoch;
static byte level;

struct Level { void (*fn)(void); };

void reset(void);

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

static void inc10(word *num) {
    byte *ptr = (byte *) num;
    if (inc10_byte(ptr + 0) == 0) {
	inc10_byte(ptr + 1);
    }
}

static void put_tile(byte cell, word n) {
    byte x = n & 0x1f;
    byte y = (n >> 2) & ~7;
    BYTE(0x5800 + n) = tiles_color[cell];
    const byte *addr = tiles + (cell << 3);
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

static void rolling_rock_sound(void) {
    for (byte i = 0; i < 4; i++) {
	byte offset = 0x10 << i;
	beep(768 - (offset << 1), 768 + offset, 64);
    }
}

static void bite(word dst) {
    for (byte i = 0; i < 4; i++) {
	put_tile(36 + i, dst);
	bite_sound(i);
    }
}

static byte roll_rock(int8 diff) {
    word dst = pos + (diff << 1);
    if ((forest[dst] & (C_TILE | C_SIZE)) == 0) {
	forest[dst] = T_ROCK;
	rolling_rock_sound();
	put_tile(34, dst);
	return TRUE;
    }
    return FALSE;
}

static byte can_move_into(byte next, int8 diff) {
    return next < C_TILE || (next == T_ROCK && roll_rock(diff));
}

static void move_hunter(int8 diff) {
    word dst = pos + diff;
    if (can_move_into(forest[dst], diff)) {
	byte *place = forest + pos;
	byte cell = *place;
	*place = C_BARE;
	tile_ptr(place);
	QUEUE(place);

	if (forest[dst] & C_SIZE) bite(dst);
	byte face = get_face(diff, cell);
	forest[dst] = C_PLAY | face;
	put_tile(face ? 33 : 32, dst);
	pos = dst;
    }
}

static void put_hunter(word where) {
    pos = where;
    move_hunter(0);
}

static void put_item(word where, byte type, byte sprite) {
    forest[where] = type;
    QUEUE(forest + where);
    put_tile(sprite, where);
}

static void put_rock(word where) {
    put_item(where, T_ROCK, 34);
}

static void put_life(word where, byte cell) {
    put_item(where, cell, cell);
}

static byte space_of_enter(void) {
    return (~in_fe(0x7f) & 1) | ((~in_fe(0xbf) & 1) << 1);
}

static void wait_space_or_enter(void (*callback)(void)) {
    byte prev, next = space_of_enter();
    do {
	prev = next;
	next = space_of_enter();
	if (callback != 0 && vblank == 1) {
	    callback();
	    vblank = 0;
	}
    } while ((next & (prev ^ next)) == 0);
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
    while (*ptr) {
	tile_ptr(*ptr);
	ptr++;
    }
    wait_user_input();
    *queue = 0;
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

static const byte *sprite;
static const byte *sprite_color;
static void put_sprite(byte cell, byte base, word n) {
    byte x = n & 0x1f;
    byte y = (n >> 2) & ~7;
    byte index = base + (cell & 0x1f);
    BYTE(0x5800 + n) = sprite_color[index];
    if (index) forest[n] = 0x80 | index;
    const byte *addr = sprite + (index << 3);
    byte flipH = cell & 0x40;
    byte flipV = cell & 0x20;
    for (byte i = 0; i < 8; i++) {
	byte data = *addr++;
	if (flipH) data = flip_bits(data);
	map_y[y + (flipV ? 7 - i : i)][x] = data;
    }
}

static void put_level(byte cell, byte base, word n) {
    if (cell || base) {
	put_sprite(cell, base, n);
    }
    else if (forest[n] == C_FOOD) {
	put_tile(C_FOOD, n);
    }
}

static void display_level(byte *level, word size, word n) {
    byte base = 0;
    for (word i = 0; i < size; i++) {
	byte cell = level[i];
	switch (cell & 0xc0) {
	case 0x80:
	    byte count = cell & 0x7f;
	    byte repeat = level[++i];
	    for (byte j = 0; j < count; j++) {
		put_level(repeat, base, n++);
	    }
	    break;
	case 0xc0:
	    base = cell & 0x3f;
	    break;
	default:
	    put_level(cell, base, n++);
	    break;
	}
    }
}

static void increment_epoch(void) {
    inc10(&epoch);
    put_num(epoch, POS(7, 23), 5);
}

static int8 (*finish)(word *);

static int8 game_round(byte **src, byte **dst) {
    queue = dst;
    advance_forest(src);
    display_forest(dst);
    increment_epoch();
    return finish(dst);
}

static void reset_memory(void) {
    memset(update, 0x00, sizeof(update));
    memset(mirror, 0x00, sizeof(mirror));
    memset(forest, 0x00, sizeof(forest));
    level = 0;
}

static byte no_grazers(word *job) {
    if (queue - job < 8) {
	word grazers = 0;
	for (word i = 0; i < SIZE(forest); i++) {
	    byte cell = forest[i];
	    if (C_FOOD < cell && cell < C_PLAY) {
		grazers++;
	    }
	}
	return grazers == 0;
    }
    else {
	return 0;
    }
}

static int8 ending_500(word *job) {
    if (epoch >= 0x500) {
	return 1;
    }
    else if (no_grazers(job)) {
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

static byte no_vegetaion(void) {
    for (word i = 0; i < SIZE(forest); i++) {
	byte cell = forest[i];
	if (cell > 0 && cell <= 3) return 0;
    }
    return 1;
}

static int8 ending_vegetation(word *job) {
    if (no_grazers(job)) {
	if (no_empty_spaces()) {
	    return 1;
	}
	else if (no_vegetaion()) {
	    return -1;
	}
    }
    return 0;
}

static void adat_meitas(void);
static void finish_game(void) {
    clear_screen();
    adat_meitas();
    reset();
}

static void fenced_level(byte *level, word size, byte init) {
    sprite = fence;
    sprite_color = fence_color;
    memset(forest, init, SIZE(forest));
    display_level(level, size, 0);
}

static void put_bones(word n) {
    static const byte id[] = { 2, 1, 35, 1, 2 };
    for (byte i = 0; i < SIZE(id); i++) {
	put_tile(id[i], (n - 2) + i);
    }
}

static void quarantine_level(void) {
    put_str("- QUARANTINE -", POS(9, 4), 0x44);
    put_str("Prevent grazer population", POS(3, 16), 4);
    put_str("from collapse til EPOCH:500", POS(2, 17), 4);
    wait_space_or_enter(0);

    fenced_level(quarantine_map, SIZE(quarantine_map), C_FOOD);

    put_hunter(POS(8, 8));
    put_life(POS(23, 14), 7);

    put_bones(POS( 2,  7));
    put_bones(POS(23, 19));
    put_bones(POS(28,  4));

    finish = &ending_500;
}

static void gardener_level(void) {
    put_str("- GARDENER -", POS(10, 4), 0x44);
    put_str("Hunt down invasive grazer", POS(4, 16), 4);
    put_str("species so that vegetation", POS(3, 17), 4);
    put_str("can fully recover and regrow.", POS(2, 18), 4);
    wait_space_or_enter(0);

    fenced_level(gardener_map, SIZE(gardener_map), C_FOOD);

    put_hunter(POS(2, 2));
    put_life(POS(29, 20), 7);
    finish = &ending_vegetation;
}

static const struct Level all_levels[] = {
    { &gardener_level },
    { &quarantine_level },
    { &finish_game },
};

static void init_variables(void) {
    epoch = 0;
    queue = update;
    all_levels[level].fn();
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
	    beep(period >> (j & 1), period << (j & 3), 150);
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
	    word period = tune[i] << n;
	    beep(period, period, delay[i] << 8);
	}
    }
    for (byte i = 0; i < 4; i++) {
	beep(4 * NOTE(196.0), 4 * NOTE(196.0), 256);
	beep(8 * NOTE(196.0), 8 * NOTE(196.0), 256);
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
    while (!space_of_enter() && melody < 2) {
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
    put_str("+--------+", POS(11, 10), 5);
    put_str(text_message, POS(11, 11), 5);
    put_str("+--------+", POS(11, 12), 5);
}

static void game_loop(void) {
    clear_screen();
    init_variables();

    int8 ending;

    do {
	ending = game_round(update, mirror);
	if (ending) break;
	ending = game_round(mirror, update);
    } while (!ending);

    switch (ending) {
    case  1:
	display_msg("|  DONE  |");
	success_tune();
	level++;
	break;
    case -1:
	display_msg("| FAILED |");
	sad_trombone_wah_wah_wah();
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

static byte eat;
static void animate_title(void) {
    byte roll = (eat >> 1) & 0x3f;
    byte frame = (eat & 0x3f) < 32;
    put_tile(frame ? 0x1e : 0x1f, POS(12, 3));
    put_tile(frame ? 0x1b : 0x1a, POS(22, 21));
    byte feed = (frame ? 0x0e : 0x0f);
    put_tile(eat > 192 ? 0xc : feed, POS( 7, 4));
    title_flash(roll - 0x10, 0x04);
    title_flash(roll - 0x08, 0x44);
    eat++;
}

static void title_screen(void) {
    clear_screen();
    sprite = logo;
    sprite_color = mirror;
    memset(mirror, 0, sizeof(logo_color));
    display_level(logo_map, SIZE(logo_map), 0x100);
    put_str("Use WASD keys to move hunter.", POS(2, 16), 5);
    put_str("Press ENTER to fast forward.", POS(2, 17), 5);
    put_str("SPACE will skip one epoch.", POS(3, 18), 5);

    grass_stripe(POS( 6, 2), 11);
    grass_stripe(POS( 3, 3), 9);
    grass_stripe(POS(13, 3), 6);
    grass_stripe(POS( 1, 4), 6);
    grass_stripe(POS( 8, 4), 8);
    grass_stripe(POS( 3, 5), 11);

    grass_stripe(POS(14, 21), 8);
    grass_stripe(POS(23, 21), 6);

    wait_space_or_enter(&animate_title);
}

void reset(void) {
    SETUP_STACK();
    setup_system();
    precalculate();
    reset_memory();
    title_screen();

    for (;;) game_loop();
}

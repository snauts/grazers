#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>

static char *file_name;
static int color_index = 1;
static unsigned char inkmap[256];
static unsigned char palette[256];
static int need_compress = 0;
static int as_tiles = 0;
static int as_level = 0;

struct Header {
    unsigned short w, h;
} header;

static void hexdump(unsigned char *buf, int size) {
    for (int i = 0; i < size; i++) {
	fprintf(stderr, "%02x ", buf[i]);
	if ((i & 0xf) == 0xf) {
	    fprintf(stderr, "\n");
	}
    }
    if ((size & 0xf) != 0x0) fprintf(stderr, "\n");
}

static void remove_extension(char *src, char *dst) {
    for (int i = 0; i < strlen(src); i++) {
	if (src[i] == '.') {
	    dst[i] = 0;
	    return;
	}
	else if (src[i] == '/') {
	    dst[i] = '_';
	}
	else {
	    dst[i] = src[i];
	}
    }
}

static unsigned char consume_pixels(unsigned char *buf, unsigned char on) {
    unsigned char ret = 0;
    for (int i = 0; i < 8; i++) {
	ret = ret << 1;
	ret |= (buf[i] == on) ? 1 : 0;
    }
    return ret;
}

static void add_color(unsigned char pixel) {
    if (pixel > 0 && inkmap[pixel] == 0) {
	inkmap[pixel] = palette[color_index++];
    }
}

static unsigned short encode_pixel(unsigned char a, unsigned char b) {
    return a > b ? (b << 8) | a : (a << 8) | b;
}

static unsigned short on_pixel(unsigned char *buf, int i, int w) {
    unsigned char pixel = buf[i];
    for (int y = 0; y < 8; y++) {
	for (int x = 0; x < 8; x++) {
	    unsigned char next = buf[i + x];
	    if (next != pixel) {
		add_color(next);
		add_color(pixel);
		return encode_pixel(next, pixel);
	    }
	}
	i += w;
    }
    add_color(pixel);
    return pixel == 0 ? 0x1 : pixel;
}

static int ink_index(int i) {
    return (i / header.w / 8) * (header.w / 8) + i % header.w / 8;
}

static unsigned char encode_ink(unsigned short colors) {
    unsigned char f = inkmap[colors & 0xff];
    unsigned char b = inkmap[colors >> 8];
    unsigned char l = (f > 7 || b > 7) ? 0x40 : 0x00;
    return l | (f & 7) | ((b & 7) << 3);
}

static int has_any_color(void) {
    if (as_level) return 0;
    for (int i = 0; i < 256; i++) {
	if (palette[i] != 0) return 1;
    }
    return 0;
}

static void dump_buffer(void *ptr, int size, int step) {
    for (int i = 0; i < size; i++) {
	if (step == 1) {
	    printf(" 0x%02x,", * (unsigned char *) ptr);
	}
	else {
	    printf(" 0x%04x,", * (unsigned short *) ptr);
	}
	if ((i & 7) == 7) printf("\n");
	ptr += step;
    }
    if ((size & 7) != 0) printf("\n");
}

static void convert_to_stripe(int w, int h, unsigned char *output) {
    int n = 0;
    int size = w * h / 8;
    unsigned char tmp[size];
    for (int y = 0; y < h; y += 8) {
	for (int x = 0; x < w / 8; x++) {
	    for (int i = 0; i < 8; i++) {
		tmp[n++] = output[((y + i) * w / 8) + x];
	    }
	}
    }
    memcpy(output, tmp, size);
}

static unsigned char flip_bits(unsigned char source) {
    unsigned char result = 0;
    for (int i = 0; i < 8; i++) {
	result = result << 1;
	result |= source & 1;
	source = source >> 1;
    }
    return result;
}

static int matchDIR(void *pixel, int n, unsigned char *tiles, int i, int d) {
    unsigned long *ptr = pixel + n;
    unsigned long flip = 0;
    for (int k = 0; k < 8; k++) {
	unsigned char byte = tiles[i + ((d & 1) ? k : 7 - k)];
	if (d & 2) byte = flip_bits(byte);
	flip = flip << 8;
	flip = flip | byte;
    }
    // if (d == 4) flip = ~flip;
    return *ptr == flip;
}

static int match(unsigned char *pixel,
		 unsigned char *tiles,
		 unsigned char *color,
		 unsigned char *extra,
		 int n, int i) {

    for (int dir = 0; dir < 4; dir++) {
	if (matchDIR(pixel, n, tiles, i, dir)) {
	    return color[n / 8] == extra[i / 8] ? dir + 1 : 0;
	}
    }
    return 0;
}

static void compress(unsigned char *pixel, int *pixel_size,
		     unsigned char *color, int *color_size) {

    int compress_size = 0;
    unsigned char tiles[*pixel_size];
    unsigned char extra[*color_size];
    for (int n = 0; n < *pixel_size; n += 8) {
	int have_match = 0;
	for (int i = 0; i < compress_size; i += 8) {
	    if (match(pixel, tiles, color, extra, n, i)) {
		have_match = 1;
		break;
	    }
	}
	if (!have_match) {
	    memcpy(tiles + compress_size, pixel + n, 8);
	    extra[compress_size / 8] = color[n / 8];
	    compress_size += 8;
	}
    }
    *pixel_size = compress_size;
    memcpy(pixel, tiles, *pixel_size);
    *color_size = compress_size / 8;
    memcpy(color, extra, *color_size);

    int fd = open("tileset.bin", O_CREAT | O_RDWR, 0644);
    if (fd >= 0) {
	write(fd, pixel_size, sizeof(int));
	write(fd, pixel, *pixel_size);
	write(fd, color_size, sizeof(int));
	write(fd, color, *color_size);
	close(fd);
    }
}

static void rle_encode(unsigned char *pixel, unsigned char *table, int *size) {
    int count = 0, done = 0;
    unsigned char prev = 0xff;
    void encode_pixel(void) {
	if (count > 1) {
	    pixel[done++] = 0x80 | count;
	}
	pixel[done++] = prev;
    }
    for (int i = 0; i < *size; i++) {
	if (table[i] == prev && count < 63) {
	    count++;
	}
	else {
	    if (prev < 0xff) {
		encode_pixel();
	    }
	    count = 1;
	}
	prev = table[i];
    }
    encode_pixel();
    *size = done;
}

static void to_level(unsigned char *pixel, int *pixel_size,
		     unsigned char *color, int *color_size) {

    int tiles_size, extra_size;
    unsigned char tiles[*pixel_size];
    unsigned char extra[*color_size];
    unsigned char table[*color_size * 2];

    int fd = open("tileset.bin", O_RDONLY, 0644);
    if (fd >= 0) {
	read(fd, &tiles_size, sizeof(int));
	read(fd, tiles, tiles_size);
	read(fd, &extra_size, sizeof(int));
	read(fd, extra, extra_size);
	close(fd);
    }
    else {
	fprintf(stderr, "ERROR: missing tileset.bin\n");
	exit(-1);
    }

    int base = 0, done = 0;
    for (int n = 0; n < *pixel_size; n += 8) {
	int found = 0;
	for (int i = 0; i < tiles_size; i += 8) {
	    int matching = match(pixel, tiles, color, extra, n, i);
	    if (matching) {
		int index = (i / 8);
		if (index > 31 + 63) {
		    fprintf(stderr, "ERROR: too many tiles\n");
		    exit(-1);
		}
		if (index > base + 31) {
		    base = index - 31;
		    table[done++] = 0xc0 | base;
		}
		if (index < base) {
		    base = index;
		    table[done++] = 0xc0 | base;
		}
		table[done++] = (index - base) | ((matching - 1) << 5);
		found = 1;
		break;
	    }
	}
	if (!found) {
	    fprintf(stderr, "ERROR: tile not found\n");
	    exit(-1);
	}
    }

    *pixel_size = done;
    rle_encode(pixel, table, pixel_size);
}

static void save(unsigned char *pixel, int pixel_size,
		 unsigned char *color, int color_size) {

    char name[256];
    remove_extension(file_name, name);
    printf("const byte %s%s[] = {\n", name, as_level ? "_bmp" : "");
    dump_buffer(pixel, pixel_size, 1);
    printf("};\n");
    if (has_any_color()) {
	printf("const byte %s_color[] = {\n", name);
	dump_buffer(color, color_size, 1);
	printf("};\n");
    }
}

static void save_bitmap(unsigned char *buf, int size) {
    int j = 0;
    int pixel_size = size / 8;
    int color_size = size / 64;
    unsigned char pixel[pixel_size];
    unsigned char color[color_size];
    unsigned short on[color_size];
    for (int i = 0; i < size; i += 8) {
	if (i / header.w % 8 == 0) {
	    on[j++] = on_pixel(buf, i, header.w);
	}
	unsigned char data = on[ink_index(i)] & 0xff;
	pixel[i / 8] = consume_pixels(buf + i, data);
    }
    for (int i = 0; i < color_size; i++) {
	color[i] = encode_ink(on[i]);
    }
    if (as_tiles) {
	convert_to_stripe(header.w, header.h, pixel);
    }
    if (as_level) {
	to_level(pixel, &pixel_size, color, &color_size);
    }
    if (need_compress) {
	compress(pixel, &pixel_size, color, &color_size);
    }
    save(pixel, pixel_size, color, color_size);
}

static unsigned char *read_pcx(const char *file) {
    struct stat st;
    int palette_offset = 16;
    if (stat(file, &st) != 0) {
	printf("ERROR while opening PCX-file \"%s\"\n", file);
	exit(-ENOENT);
    }
    unsigned char *buf = malloc(st.st_size);
    int in = open(file, O_RDONLY);
    read(in, buf, st.st_size);
    close(in);

    header.w = (* (unsigned short *) (buf + 0x8)) + 1;
    header.h = (* (unsigned short *) (buf + 0xa)) + 1;
    if (buf[3] == 8) palette_offset = st.st_size - 768;
    int unpacked_size = header.w * header.h / (buf[3] == 8 ? 1 : 2);
    unsigned char *pixels = malloc(unpacked_size);

    int i = 128, j = 0;
    while (j < unpacked_size) {
	if ((buf[i] & 0xc0) == 0xc0) {
	    int count = buf[i++] & 0x3f;
	    while (count-- > 0) {
		pixels[j++] = buf[i];
	    }
	    i++;
	}
	else {
	    pixels[j++] = buf[i++];
	}
    }
    free(buf);
    return pixels;
}

int main(int argc, char **argv) {
    if (argc < 2) {
	printf("USAGE: tga-dump [option] file.pcx\n");
	printf("  -c   save compressed zx\n");
	printf("  -b   save bitmap zx\n");
	printf("  -t   save tiles zx\n");
	printf("  -l   save level zx\n");
	return 0;
    }

    file_name = argv[2];
    unsigned char *buf = read_pcx(file_name);

    switch (argv[1][1]) {
    case 'l':
	as_level = 1;
	goto tiles;
    case 'c':
	need_compress = 1;
	goto tiles;
    case 't':
    tiles:
	as_tiles = 1;
	/* falls through */
    case 'b':
#ifdef ZXS
	for (int i = 3; i < argc; i++) {
	    palette[i - 2] = atoi(argv[i]);
	}	
	memset(inkmap, 0, sizeof(inkmap));
	save_bitmap(buf, header.w * header.h);
#endif
	break;
    }
    free(buf);
    return 0;
}

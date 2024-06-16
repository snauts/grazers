#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>

// #define DEBUG

static char *file_name;
static int color_index = 1;
static unsigned char inkmap[256];
static unsigned char palette[256];
static int need_compress = 0;
static int as_tiles = 0;
static int as_level = 0;

struct Header {
    unsigned char id;
    unsigned char color_type;
    unsigned char image_type;
    unsigned char color_map[5];
    unsigned short x, y, w, h;
    unsigned char depth;
    unsigned char desc;
};

#ifdef DEBUG
static void hexdump(unsigned char *buf, int size) {
    for (int i = 0; i < size; i++) {
	fprintf(stderr, "%02x ", buf[i]);
	if ((i & 0xf) == 0xf) {
	    fprintf(stderr, "\n");
	}
    }
    if ((size & 0xf) != 0x0) fprintf(stderr, "\n");
}
#endif

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

static int ink_index(struct Header *header, int i) {
    return (i / header->w / 8) * (header->w / 8) + i % header->w / 8;
}

static unsigned char encode_ink(unsigned short colors) {
    unsigned char f = inkmap[colors & 0xff];
    unsigned char b = inkmap[colors >> 8];
    unsigned char l = (f > 7 || b > 7) ? 0x40 : 0x00;
    return l | (f & 7) | ((b & 7) << 3);
}

static int has_any_color(void) {
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

static int matchDIR(void *pixels, int n, unsigned char *tiles, int i, int d) {
    unsigned long *ptr = pixels + n;
    unsigned long flip = 0;
    for (int k = 0; k < 8; k++) {
	unsigned char byte = tiles[i + ((d & 1) ? k : 7 - k)];
	if (d & 2) byte = flip_bits(byte);
	flip = flip << 8;
	flip = flip | byte;
    }
    if (d == 4) flip = ~flip;
    return *ptr == flip;
}

static int match(unsigned char *pixels, int n, unsigned char *tiles, int i) {
    for (int dir = 0; dir < 4; dir++) {
	if (matchDIR(pixels, n, tiles, i, dir)) {
	    return 1;
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
	    int color_match = color[n / 8] == extra[i / 8];
	    if (match(pixel, n, tiles, i) && color_match) {
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

static void save(unsigned char *pixel, int pixel_size,
		 unsigned char *color, int color_size) {

    char name[256];
    remove_extension(file_name, name);
    printf("const byte %s[] = {\n", name);
    dump_buffer(pixel, pixel_size, 1);
    printf("};\n");
    if (has_any_color()) {
	printf("const byte %s_color[] = {\n", name);
	dump_buffer(color, color_size, 1);
	printf("};\n");
    }
}

static void save_bitmap(struct Header *header, unsigned char *buf, int size) {
    int j = 0;
    int pixel_size = size / 8;
    int color_size = size / 64;
    unsigned char pixel[pixel_size];
    unsigned char color[color_size];
    unsigned short on[color_size];
    for (int i = 0; i < size; i += 8) {
	if (i / header->w % 8 == 0) {
	    on[j++] = on_pixel(buf, i, header->w);
	}
	unsigned char data = on[ink_index(header, i)] & 0xff;
	pixel[i / 8] = consume_pixels(buf + i, data);
    }
    for (int i = 0; i < color_size; i++) {
	color[i] = encode_ink(on[i]);
    }
    if (as_tiles) {
	convert_to_stripe(header->w, header->h, pixel);
    }
    if (need_compress) {
	compress(pixel, &pixel_size, color, &color_size);
    }
    save(pixel, pixel_size, color, color_size);
}

int main(int argc, char **argv) {
    if (argc < 2) {
	printf("USAGE: tga-dump [option] file.tga\n");
	printf("  -c   save compressed zx\n");
	printf("  -b   save bitmap zx\n");
	printf("  -t   save tiles zx\n");
	printf("  -l   save level zx\n");
	return 0;
    }

    file_name = argv[2];
    int fd = open(file_name, O_RDONLY);
    if (fd < 0) {
	printf("ERROR: unable to open %s\n", file_name);
	return -ENOENT;
    }

    struct Header header;
    read(fd, &header, sizeof(header));

    if (header.image_type != 3 || header.depth != 8) {
	printf("ERROR: not a grayscale 8-bit TGA file\n");
	close(fd);
	return 0;
    }

    int size = header.w * header.h;
    unsigned char buf[size];
    read(fd, buf, size);
    close(fd);

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
	save_bitmap(&header, buf, size);
#endif
	break;
    }

    return 0;
}

#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <arpa/inet.h>

const char *file = "grazers.bin";

#define min(a, b) ((a) < (b) ? (a) : (b))

int main(int argc, char **argv) {
    struct stat st;
    if (stat(file, &st) != 0) {
	printf("ERROR \"%s\" not found\n", file);
	exit(-ENOENT);
    }
    if (st.st_size > 0x7ff0) {
	printf("ERROR \"%s\" too large\n", file);
	exit(-ENOENT);
    }
    unsigned char *buf = malloc(0x8000);
    memset(buf, 0, sizeof(buf));

    int in = open(file, O_RDONLY);
    read(in, buf, st.st_size);
    close(in);

    unsigned short sum = 0;
    for (int i = 0; i < 0x7ff0; i++) {
	sum += buf[i];
    }
    printf("SUM=[%04x]\n", sum);
    strcpy(buf + 0x7ff0, "TMR SEGA");
    buf[0x7ffa] = sum & 0xff;
    buf[0x7ffb] = sum >> 8;
    buf[0x7fff] = 0x4c; /* export 32KB */

    int fd = open("grazers.sms", O_CREAT | O_RDWR, 0644);
    if (fd >= 0) {
	write(fd, buf, 0x8000);
	close(fd);
    }

    return 0;
}

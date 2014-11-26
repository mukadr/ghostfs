/*
 * Steganography interface
 *
 * Multiple steganography methods implement this interface
 */
#ifndef GHOST_STEG_H
#define GHOST_STEG_H

#include <stddef.h>

#define container_of(obj, type, member) ((type *)((char *)obj - (char *)offsetof(type, member)))

enum steg_type {
	STEG_WAV,
};

struct steg;

struct steg_ops {
	ssize_t (*read)(struct steg *steg, void *buf, size_t size, size_t offset, int bits);
	ssize_t (*write)(struct steg *steg, const void *buf, size_t size, size_t offset, int bits);
	void (*release)(struct steg *steg);
	size_t (*capacity)(struct steg *steg);
};

struct steg {
	int fd;
	unsigned char *map;
	size_t len;
	const struct steg_ops *ops;
};

int steg_init(struct steg *steg, const char *filename, const struct steg_ops *ops);
int steg_open(struct steg **steg, const char *filename);
int steg_close(struct steg *steg);

ssize_t steg_read(struct steg *steg, void *buf, size_t size, size_t offset, int bits);
ssize_t steg_write(struct steg *steg, const void *buf, size_t size, size_t offset, int bits);
size_t steg_capacity(struct steg *steg);

#endif // GHOST_STEG_H

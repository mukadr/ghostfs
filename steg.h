/*
 * Steganography interface
 */
#ifndef GHOST_STEG_H
#define GHOST_STEG_H

#include <stddef.h>

#define container_of(obj, type, member) ((type *)((char *)obj - (char *)offsetof(type, member)))

struct steg {
	int fd;
	unsigned char *map;
	size_t len;
	const struct steg_operations *ops;
};

struct steg_operations {
	int (*read)(struct steg *steg, void *buf, size_t size, size_t offset);
	int (*write)(struct steg *steg, const void *buf, size_t size, size_t offset);
	void (*release)(struct steg *steg);
	size_t (*capacity)(struct steg *steg);
};

int steg_init(struct steg *steg, const char *filename, const struct steg_operations *ops);
int steg_open(struct steg **steg, const char *filename);
int steg_close(struct steg *steg);

int steg_read(struct steg *steg, void *buf, size_t size, size_t offset);
int steg_write(struct steg *steg, const void *buf, size_t size, size_t offset);
size_t steg_capacity(struct steg *steg);

#endif // GHOST_STEG_H

/*
 * Steganography interface
 *
 * Multiple steganography methods implement this interface
 */
#ifndef GHOST_STEG_H
#define GHOST_STEG_H

#define container_of(obj, type, member) ((type *)((char *)obj - (char *)offsetof(type, member)))

struct steg;

struct steg_ops {
	ssize_t (*read)(struct steg *steg, void *buf, size_t size, size_t offset, int bits);
	ssize_t (*write)(struct steg *steg, const void *buf, size_t size, size_t offset, int bits);
	void (*release)(struct steg *steg);
};

struct steg {
	int fd;
	unsigned char *map;
	size_t len;
	struct steg_ops ops;
};

int steg_open(struct steg *steg, const char *filename, const struct steg_ops *ops);
int steg_close(struct steg *steg);

ssize_t steg_read(struct steg *steg, void *buf, size_t size, size_t offset, int bits);
ssize_t steg_write(struct steg *steg, const void *buf, size_t size, size_t offset, int bits);

#endif // GHOST_STEG_H

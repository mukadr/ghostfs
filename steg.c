#include <err.h>
#include <limits.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

#include "steg.h"
#include "steg_wav.h"

int steg_init(struct steg *steg, const char *filename, const struct steg_ops *ops)
{
	struct stat st;
	int fd;

	fd = open(filename, O_RDWR);
	if (fd < 0) {
		warn("steg: open");
		return -1;
	}
	if (fstat(fd, &st) < 0) {
		close(fd);
		warn("steg: stat");
		return -1;
	}
	steg->fd = fd;
	steg->len = st.st_size;
	steg->map = mmap(NULL, steg->len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (steg->map == MAP_FAILED) {
		close(fd);
		warn("steg: mmap");
		return -1;
	}
	steg->ops = ops;
	return 0;
}

int steg_open(struct steg **steg, const char *filename)
{
	size_t len;

	len = strlen(filename);
	if (len < 5) {
		warnx("steg: unknown file type");
		return -1;
	}

	if (memcmp(&filename[len-4], ".wav", 4) == 0) {
		return wav_open(steg, filename);
	}
	warnx("steg: unknown file type");
	return -1;
}

int steg_close(struct steg *steg)
{
	int ret = 0;

	if (munmap(steg->map, steg->len) < 0) {
		warn("steg: munmap");
		ret = -1;
	}
	if (close(steg->fd) < 0) {
		warn("steg: close");
		ret = -1;
	}
	steg->ops->release(steg);
	return ret;
}

ssize_t steg_read(struct steg *steg, void *buf, size_t size, size_t offset, int bits)
{
	return steg->ops->read(steg, buf, size, offset, bits);
}

ssize_t steg_write(struct steg *steg, const void *buf, size_t size, size_t offset, int bits)
{
	return steg->ops->write(steg, buf, size, offset, bits);
}

size_t steg_capacity(struct steg *steg)
{
	return steg->ops->capacity(steg);
}

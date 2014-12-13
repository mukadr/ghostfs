#include <errno.h>
#include <limits.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

#include "steg.h"
#include "steg_wav.h"

int steg_init(struct steg *steg, const char *filename, const struct steg_operations *ops)
{
	struct stat st;
	int fd;
	int ret;

	fd = open(filename, O_RDWR);
	if (fd < 0)
		return -errno;

	if (fstat(fd, &st) < 0) {
		ret = -errno;
		close(fd);
		return ret;
	}

	steg->map = mmap(NULL, st.st_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (steg->map == MAP_FAILED) {
		ret = -errno;
		close(fd);
		return ret;
	}

	steg->fd = fd;
	steg->len = st.st_size;
	steg->ops = ops;

	return 0;
}

int steg_open(struct steg **steg, const char *filename)
{
	size_t len;

	len = strlen(filename);
	if (len < 5)
		return -EMEDIUMTYPE;

	if (memcmp(&filename[len-4], ".wav", 4) == 0)
		return wav_open(steg, filename);

	return -EMEDIUMTYPE;
}

int steg_close(struct steg *steg)
{
	int ret = 0;

	if (munmap(steg->map, steg->len) < 0)
		ret = -errno;

	if (close(steg->fd) < 0)
		ret = -errno;

	steg->ops->release(steg);

	return ret;
}

int steg_read(struct steg *steg, void *buf, size_t size, size_t offset, int bits)
{
	return steg->ops->read(steg, buf, size, offset, bits);
}

int steg_write(struct steg *steg, const void *buf, size_t size, size_t offset, int bits)
{
	return steg->ops->write(steg, buf, size, offset, bits);
}

size_t steg_capacity(struct steg *steg)
{
	return steg->ops->capacity(steg);
}

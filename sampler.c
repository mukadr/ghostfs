#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "sampler.h"

int sampler_init(struct sampler *sampler, const char *filename)
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

	sampler->map = mmap(NULL, st.st_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (sampler->map == MAP_FAILED) {
		ret = -errno;
		close(fd);
		return ret;
	}

	sampler->fd = fd;
	sampler->size = st.st_size;

	return 0;
}

int sampler_close(struct sampler *sampler)
{
	if (munmap(sampler->map, sampler->size) < 0) {
		int ret = -errno;
		close(sampler->fd);
		sampler->close(sampler);
		return ret;
	}

	if (close(sampler->fd) < 0) {
		sampler->close(sampler);
		return -errno;
	}

	return sampler->close(sampler);
}

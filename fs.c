#include <err.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "fs.h"
#include "steg_wav.h"

enum {
	CLUSTER_SIZE = 4096,
};

struct ghostfs_header {
	uint16_t clusters;
} __attribute__((packed));

struct ghostfs {
	int status;
	struct ghostfs_header hdr;
	struct steg *steg;
};

// check if there is a filesystem
static void ghostfs_check(const struct ghostfs *gfs)
{
}

// create a new filesystem
int ghostfs_format(struct ghostfs *gfs)
{
	size_t avail;
	size_t clusters;

	avail = steg_capacity(gfs->steg) - sizeof(struct ghostfs_header);
	clusters = avail / CLUSTER_SIZE;

	if (clusters < 1) {
		warnx("ghostfs: no minimum space available");
		return -1;
	}
	if (clusters > 0xFFFF)
		clusters = 0xFFFF;

	gfs->hdr.clusters = clusters;

	// write header
	if (steg_write(gfs->steg, &gfs->hdr, sizeof(struct ghostfs_header), 0, 1) < 0)
		return -1;

	return 0;
}

int ghostfs_status(const struct ghostfs *gfs)
{
	return gfs->status;
}

int ghostfs_open(struct ghostfs **pgfs, const char *filename)
{
	struct ghostfs *gfs;
	enum steg_type type;
	size_t len;

	len = strlen(filename);
	if (len < 5) {
		warnx("ghostfs: unknown file type");
		return -1;
	}

	if (memcmp(&filename[len-4], ".wav", 4) == 0) {
		type = STEG_WAV;
	} else {
		warnx("ghostfs: unknown file type");
		return -1;
	}

	gfs = malloc(sizeof(*gfs));
	if (!gfs) {
		warn("malloc");
		return -1;
	}

	switch (type) {
	case STEG_WAV:
		if (wav_open(&gfs->steg, filename) < 0) {
			free(gfs);
			return -1;
		}
		break;
	}

	ghostfs_check(gfs);

	*pgfs = gfs;
	return 0;
}

int ghostfs_close(struct ghostfs *gfs)
{
	int ret;
	ret = steg_close(gfs->steg);
	free(gfs);
	return ret;
}

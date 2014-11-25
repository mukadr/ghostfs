#include <err.h>
#include <stdlib.h>
#include <string.h>

#include "fs.h"
#include "steg_wav.h"

struct ghostfs {
	struct steg *steg;
};

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

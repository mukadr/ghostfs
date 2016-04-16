#include <err.h>
#include <errno.h>
#include <string.h>

#include "bmp.h"
#include "fs.h"
#include "lsb.h"
#include "util.h"
#include "wav.h"

int open_sampler_by_extension(struct sampler **sampler, const char *filename)
{
	size_t len;

	len = strlen(filename);
	if (len < 5)
		return -EIO;

	if (memcmp(&filename[len-4], ".bmp", 4) == 0)
		return bmp_open(sampler, filename);

	if (memcmp(&filename[len-4], ".wav", 4) == 0)
		return wav_open(sampler, filename);

	return -EIO;
}

int try_mount_lsb(struct ghostfs **pgfs, struct stegger **plsb, struct sampler *sampler)
{
	struct stegger *lsb;
	struct ghostfs *gfs;
	int i, ret;

	for (i = 1; i <= 8; i++) {
		ret = lsb_open(&lsb, sampler, i);
		if (ret < 0)
			return ret;

		ret = ghostfs_mount(&gfs, lsb);
		if (ret == 0) {
			*pgfs = gfs;
			*plsb = lsb;
			return 0;
		}

		stegger_close(lsb);
	}

	warnx("tried to mount lsb 1..8: failed");

	return ret;
}


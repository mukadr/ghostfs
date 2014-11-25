#include <err.h>
#include <stdlib.h>

#include "steg_wav.h"

struct wav {
	struct steg steg;
};

static ssize_t wav_read(struct steg *steg, void *buf, size_t size, size_t offset, int bits)
{ 
	struct wav *wav = container_of(steg, struct wav, steg);
	(void)wav;
	return 0;
}

static ssize_t wav_write(struct steg *steg, const void *buf, size_t size, size_t offset, int bits)
{
	struct wav *wav = container_of(steg, struct wav, steg);
	(void)wav;
	return 0;
}

static void wav_release(struct steg *steg)
{
	struct wav *wav = container_of(steg, struct wav, steg);
	free(wav);
}

static const struct steg_ops wav_ops = {
	.read = wav_read,
	.write = wav_write,
	.release = wav_release
};

/* Parses the wav file to locate its data section */
static int wav_init(struct wav *wav)
{
	return 0;
}

int wav_open(struct steg **steg, const char *filename)
{
	struct wav *wav;

	wav = malloc(sizeof(*wav));
	if (!wav) {
		warn("malloc");
		return -1;
	}

	if (steg_open(&wav->steg, filename, &wav_ops) < 0) {
		free(wav);
		return -1;
	}

	if (wav_init(wav) < 0) {
		steg_close(&wav->steg);
		return -1;
	}

	*steg = &wav->steg;
	return 0;
}

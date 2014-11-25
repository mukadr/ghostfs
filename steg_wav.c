#include <err.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "steg_wav.h"

struct wav {
	int bps; // bits per sample
	unsigned char *data; // data section
	size_t len; // data section len
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
	unsigned char *p = wav->steg.map;
	size_t len = wav->steg.len;
	uint16_t audio_fmt;

	while (len >= 4) {
		if (memcmp(p, "fmt ", 4) == 0)
			break;
		p++;
		len--;
	}
	if (len < 24) {
		warnx("wav: incomplete or no 'fmt ' section found");
		return -1;
	}
	audio_fmt = *(uint16_t *)(p + 8);
	if (audio_fmt != 1) {
		warnx("wav: only PCM format supported");
		return -1;
	}
	wav->bps = *(uint16_t *)(p + 22);

	while (len >= 4) {
		if (memcmp(p, "data ", 4) == 0)
			break;
		p++;
		len--;
	}
	if (len < 8) {
		warnx("wav: incomplete or no 'data' section found");
		return -1;
	}
	wav->data = p + 8;
	wav->len = *(uint32_t *)(p + 4);

	if (wav->len > wav->steg.len) {
		warnx("wav: bad data section");
		return -1;
	}

	printf("bps = %d\n", wav->bps);
	printf("data size = %lu\n", wav->len);
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

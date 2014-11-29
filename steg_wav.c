#include <err.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "steg_wav.h"

struct wav {
	int bps; // bytes per sample
	unsigned char *data; // data section
	size_t len; // data section len
	struct steg steg;
};

// for now, bits is always 1
static ssize_t wav_read(struct steg *steg, void *buf, size_t size, size_t offset, int bits)
{ 
	struct wav *wav = container_of(steg, struct wav, steg);
	unsigned char *bp;
	int i;

	// translate byte offset to sample offset
	offset = offset * 8 * wav->bps;

	if (offset + size*8*wav->bps >= wav->len) {
		warnx("wav: trying to read beyond data section");
		return -1;
	}

	bp = buf;
	for (i = 0; i < size; i++) {
		unsigned char b = 0;
		int bit;

		for (bit = 0; bit < 8; bit++) {
			if ((wav->data[offset] & 1) == 1)
				b |= 1 << bit;
			offset += wav->bps;
		}
		*bp++ = b;
	}

	return size;
}

static ssize_t wav_write(struct steg *steg, const void *buf, size_t size, size_t offset, int bits)
{
	struct wav *wav = container_of(steg, struct wav, steg);
	const unsigned char *bp;
	size_t left;
	int bit;

	// translate byte offset to sample offset
	offset = offset * 8 * wav->bps;

	if (offset + size*8*wav->bps >= wav->len) {
		warnx("wav: trying to write beyond data section");
		return -1;
	}

	bp = buf;
	bit = 0;
	left = size;
	for (;;) {
		if ((bp[0] & (1 << bit)) != 0)
			wav->data[offset] |= 1;
		else
			wav->data[offset] &= 0xFE;

		bit++;
		offset += wav->bps;

		if (bit == 8) {
			bit = 0;
			if (left == 1)
				break;
			left--;
			bp++;
		}
	}

	return size;
}

static size_t wav_capacity(struct steg *steg)
{
	struct wav *wav = container_of(steg, struct wav, steg);

	return wav->len/wav->bps/8;
}

static void wav_release(struct steg *steg)
{
	struct wav *wav = container_of(steg, struct wav, steg);
	free(wav);
}

static const struct steg_ops wav_ops = {
	.read = wav_read,
	.write = wav_write,
	.capacity = wav_capacity,
	.release = wav_release,
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
	wav->bps = *(uint16_t *)(p + 22) / 8;

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

	if (wav->len + (wav->data - wav->steg.map) > wav->steg.len) {
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
		warn("wav: malloc");
		return -1;
	}

	if (steg_init(&wav->steg, filename, &wav_ops) < 0) {
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

#include <err.h>
#include <errno.h>
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

static int wav_read(struct steg *steg, void *buf, size_t size, size_t offset, int bits)
{ 
	struct wav *wav = container_of(steg, struct wav, steg);
	unsigned char *bp;
	int i, rbit;

	// translate byte offset to sample offset
	rbit = offset * 8 * wav->bps % bits;
	offset = offset * 8 * wav->bps / bits;

	if (offset + (size * 8 * wav->bps / bits) >= wav->len)
		return -EINVAL;

	bp = buf;
	for (i = 0; i < size; i++) {
		unsigned char b = 0;
		int wbit;

		for (wbit = 0; wbit < 8; wbit++) {
			if (wav->data[offset] & (1 << rbit))
				b |= 1 << wbit;

			rbit++;

			if (rbit == bits) {
				rbit = 0;
				offset += wav->bps;
			}
		}
		*bp++ = b;
	}

	return 0;
}

static int wav_write(struct steg *steg, const void *buf, size_t size, size_t offset, int bits)
{
	struct wav *wav = container_of(steg, struct wav, steg);
	const unsigned char *bp;
	int rbit, wbit;

	// translate byte offset to sample offset
	wbit = offset * 8 * wav->bps % bits;
	offset = offset * 8 * wav->bps / bits;

	if (offset + (size * 8 * wav->bps / bits) >= wav->len)
		return -EINVAL;

	bp = buf;
	rbit = 0;
	for (;;) {
		if (bp[0] & (1 << rbit))
			wav->data[offset] |= 1 << wbit;
		else
			wav->data[offset] &= ~(1 << wbit);

		rbit++;
		wbit++;

		if (wbit == bits) {
			wbit = 0;
			offset += wav->bps;
		}

		if (rbit == 8) {
			rbit = 0;
			if (size == 1)
				break;
			size--;
			bp++;
		}
	}

	return 0;
}

static size_t wav_capacity(struct steg *steg)
{
	struct wav *wav = container_of(steg, struct wav, steg);

	return wav->len/wav->bps;
}

static void wav_release(struct steg *steg)
{
	struct wav *wav = container_of(steg, struct wav, steg);
	free(wav);
}

static const struct steg_operations wav_ops = {
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
		return -EIO;
	}

	audio_fmt = *(uint16_t *)(p + 8);
	if (audio_fmt != 1) {
		warnx("wav: only PCM format supported");
		return -EIO;
	}

	wav->bps = *(uint16_t *)(p + 22) / 8;

	while (len >= 4) {
		if (memcmp(p, "data", 4) == 0)
			break;
		p++;
		len--;
	}
	if (len < 8) {
		warnx("wav: incomplete or no 'data' section found");
		return -EIO;
	}

	wav->data = p + 8;
	wav->len = *(uint32_t *)(p + 4);

	if (wav->len + (wav->data - wav->steg.map) > wav->steg.len) {
		warnx("wav: bad data section");
		return -EIO;
	}

	return 0;
}

int wav_open(struct steg **steg, const char *filename)
{
	struct wav *wav;
	int ret;

	wav = malloc(sizeof(*wav));
	if (!wav)
		return -ENOMEM;

	ret = steg_init(&wav->steg, filename, &wav_ops);
	if (ret < 0) {
		free(wav);
		return ret;
	}

	ret = wav_init(wav);
	if (ret < 0) {
		steg_close(&wav->steg);
		return ret;
	}

	*steg = &wav->steg;
	return 0;
}

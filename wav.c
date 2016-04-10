#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "wav.h"

static int parse_wav_header(struct sampler *sampler)
{
	unsigned char *p = sampler->map;
	long len = sampler->size;
	uint16_t audio_fmt;
	long data_len;

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

	sampler->bits = *(uint16_t *)(p + 22);

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

	sampler->ptr = p + 8;
	data_len = *(uint32_t *)(p + 4);

	if (data_len + (sampler->ptr - sampler->map) > sampler->size) {
		warnx("wav: bad data section");
		return -EIO;
	}

	sampler->count = data_len / (sampler->bits / 8);

	return 0;
}

int wav_close(struct sampler *sampler)
{
	free(sampler);
	return 0;
}

int wav_open(struct sampler **sampler, const char *filename)
{
	struct sampler *s;
	int ret;

	s = malloc(sizeof(*s));
	if (!s)
		return -ENOMEM;

	ret = sampler_init(s, filename);
	if (ret < 0) {
		free(s);
		return ret;
	}

	s->close = wav_close;

	ret = parse_wav_header(s);
	if (ret < 0) {
		free(s);
		return ret;
	}

	*sampler = s;

	return 0;
}

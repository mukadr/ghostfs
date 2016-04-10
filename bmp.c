#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bmp.h"
#include "util.h"

static int parse_bmp_header(struct sampler *sampler)
{
	unsigned char *map = sampler->map;
	long w, h, bpp;
	long pixel_offset;

	if (sampler->size < 30) {
		warn("bmp: invalid header");
		return -EIO;
	}

	if (map[0] != 'B' || map[1] != 'M') {
		warn("bmp: unsupported format");
		return -EIO;
	}

	pixel_offset = *(uint32_t *)(map + 10);
	w = *(uint32_t *)(map + 18);
	h = *(uint32_t *)(map + 22);
	bpp = *(uint16_t *)(map + 28) / 8;
	sampler->count = w * h * bpp;

	if (pixel_offset + sampler->count > sampler->size) {
		warn("bmp: invalid pixel offset");
		return -EIO;
	}

	sampler->ptr = map + pixel_offset;

	return 0;
}

int bmp_close(struct sampler *sampler)
{
	free(sampler);
	return 0;
}

int bmp_open(struct sampler **sampler, const char *filename)
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

	s->bits = 8;
	s->close = bmp_close;

	ret = parse_bmp_header(s);
	if (ret < 0) {
		free(s);
		return ret;
	}

	*sampler = s;

	return 0;
}

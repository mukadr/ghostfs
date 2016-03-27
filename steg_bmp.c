#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "steg_bmp.h"

struct bmp {
	unsigned char *pixel; // pixel section
	size_t len; // pixel section len (bytes)
	struct steg steg;
};

static int bmp_read(struct steg *steg, void *buf, size_t size, size_t offset, int bits)
{ 
	struct bmp *bmp = container_of(steg, struct bmp, steg);
	unsigned char *bp;
	int i, rbit;

	rbit = offset * 8 % bits;
	offset = offset * 8 / bits;
	if (offset + (size * 8 / bits) >= bmp->len)
		return -EINVAL;

	bp = buf;
	for (i = 0; i < size; i++) {
		unsigned char b = 0;
		int wbit;

		for (wbit = 0; wbit < 8; wbit++) {
			if (bmp->pixel[offset] & (1 << rbit))
				b |= 1 << wbit;

			rbit++;

			if (rbit == bits) {
				rbit = 0;
				offset++;
			}
		}
		*bp++ = b;
	}

	return 0;
}

static int bmp_write(struct steg *steg, const void *buf, size_t size, size_t offset, int bits)
{
	struct bmp *bmp = container_of(steg, struct bmp, steg);
	const unsigned char *bp;
	int rbit, wbit;

	wbit = offset * 8 % bits;
	offset = offset * 8 / bits;

	if (offset + (size * 8 / bits) >= bmp->len)
		return -EINVAL;

	bp = buf;
	rbit = 0;
	for (;;) {
		if (bp[0] & (1 << rbit))
			bmp->pixel[offset] |= 1 << wbit;
		else
			bmp->pixel[offset] &= ~(1 << wbit);

		rbit++;
		wbit++;

		if (wbit == bits) {
			wbit = 0;
			offset++;
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

static size_t bmp_capacity(struct steg *steg)
{
	struct bmp *bmp = container_of(steg, struct bmp, steg);

	return bmp->len;
}

static void bmp_release(struct steg *steg)
{
	struct bmp *bmp = container_of(steg, struct bmp, steg);
	free(bmp);
}

static const struct steg_operations bmp_ops = {
	.read = bmp_read,
	.write = bmp_write,
	.capacity = bmp_capacity,
	.release = bmp_release,
};

/* Parses the bmp file to locate its pixel array section */
static int bmp_init(struct bmp *bmp)
{
	unsigned char *p = bmp->steg.map;
	size_t len = bmp->steg.len;
	uint32_t pixel_offset;
	unsigned w, h, bpp;

	if (len < 30) {
		warnx("bmp: invalid header");
		return -EIO;
	}

	if (p[0] != 'B' || p[1] != 'M') {
		warnx("bmp: unsupported format");
		return -EIO;
	}

	w = *(uint32_t *)(p + 18);
	h = *(uint32_t *)(p + 22);
	bpp = *(uint16_t *)(p + 28) / 8;

	bmp->len = w * h * bpp;

	pixel_offset = *(uint32_t *)(p + 10);
	if (pixel_offset + bmp->len > len) {
		warnx("bmp: invalid pixel offset");
		return -EIO;
	}

	bmp->pixel = &p[pixel_offset];

	return 0;
}

int bmp_open(struct steg **steg, const char *filename)
{
	struct bmp *bmp;
	int ret;

	bmp = malloc(sizeof(*bmp));
	if (!bmp)
		return -ENOMEM;

	ret = steg_init(&bmp->steg, filename, &bmp_ops);
	if (ret < 0) {
		free(bmp);
		return ret;
	}

	ret = bmp_init(bmp);
	if (ret < 0) {
		steg_close(&bmp->steg);
		return ret;
	}

	*steg = &bmp->steg;
	return 0;
}

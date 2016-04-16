#include <err.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>

#include "lsb.h"
#include "util.h"

struct lsb {
	int bits;

	struct sampler *sampler;

	struct stegger stegger;
};

static int lsb_read(struct stegger *stegger, void *buf, size_t size, size_t offset)
{ 
	struct lsb *lsb = container_of(stegger, struct lsb, stegger);
	int rbit;
	int i;
	unsigned char *bp = buf;
	sample_t sample = 0;
	bool fetch = true;

	rbit = offset * 8 % lsb->bits;
	offset = offset * 8 / lsb->bits;

	if (offset + (size * 8 / lsb->bits) >= lsb->sampler->count) {
		warnx("lsb_read: bad offset");
		return -EINVAL;
	}

	for (i = 0; i < size; i++) {
		unsigned char b = 0;
		int wbit;

		for (wbit = 0; wbit < 8; wbit++) {
			if (fetch) {
				sample = sampler_read(lsb->sampler, offset);
				fetch = false;
			}

			if (sample & (1 << rbit))
				b |= 1 << wbit;

			rbit++;

			if (rbit == lsb->bits) {
				rbit = 0;
				offset++;
				fetch = true;
			}
		}
		*bp++ = b;
	}

	return 0;
}

static int lsb_write(struct stegger *stegger, const void *buf, size_t size, size_t offset)
{
	struct lsb *lsb = container_of(stegger, struct lsb, stegger);
	int wbit, rbit = 0;
	const unsigned char *bp = buf;
	sample_t sample = 0;
	bool fetch = true;

	wbit = offset * 8 % lsb->bits;
	offset = offset * 8 / lsb->bits;

	if (offset + (size * 8 / lsb->bits) >= lsb->sampler->count) {
		warnx("lsb_write: bad offset");
		return -EINVAL;
	}


	for (;;) {
		if (fetch) {
			sample = sampler_read(lsb->sampler, offset);
			fetch = false;
		}

		if (bp[0] & (1 << rbit))
			sample |= 1 << wbit;
		else
			sample &= ~(1 << wbit);

		rbit++;
		wbit++;

		if (wbit == lsb->bits) {
			wbit = 0;

			sampler_write(lsb->sampler, offset, sample);

			offset++;
			fetch = true;
		}

		if (rbit == 8) {
			rbit = 0;
			if (size == 1)
				break;
			size--;
			bp++;
		}
	}

	if (wbit > 0)
		sampler_write(lsb->sampler, offset, sample);

	return 0;
}

static int lsb_close(struct stegger *stegger)
{
	struct lsb *lsb = container_of(stegger, struct lsb, stegger);
	free(lsb);
	return 0;
}

int lsb_open(struct stegger **stegger, struct sampler *sampler, int bits)
{
	struct lsb *lsb;

	if (bits < 1 || bits > sampler->bits) {
		warnx("lsb_new: invalid bits");
		return -EINVAL;
	}

	lsb = malloc(sizeof(*lsb));
	if (!lsb)
		return -ENOMEM;

	lsb->stegger.capacity = sampler->count * bits / 8;
	lsb->stegger.read = lsb_read;
	lsb->stegger.write = lsb_write;
	lsb->stegger.close = lsb_close;

	lsb->sampler = sampler;
	lsb->bits = bits;

	*stegger = &lsb->stegger;

	return 0;
}

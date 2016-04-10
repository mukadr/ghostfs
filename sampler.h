#ifndef GHOST_SAMPLER_H
#define GHOST_SAMPLER_H

#include <assert.h>
#include <stdint.h>

typedef unsigned int sample_t;

struct sampler {
	int fd;
	unsigned char *map;
	long size;

	// initialized by the implementation
	unsigned char *ptr;
	long count;
	int bits;
	int (*close)(struct sampler *sampler);
};

static inline sample_t sampler_read(struct sampler *self, long nr)
{
	switch (self->bits) {
	case 8:
		return self->ptr[nr];
	case 16:
		return ((uint16_t *)self->ptr)[nr];
	case 32:
		return ((uint32_t *)self->ptr)[nr];
	default:
		assert(!"sampler_read: bad bits");
	}
}

static inline void sampler_write(struct sampler *self, long nr, sample_t sample)
{
	switch (self->bits) {
	case 8:
		self->ptr[nr] = sample;
		break;
	case 16:
		((uint16_t *)self->ptr)[nr] = sample;
		break;
	case 32:
		((uint32_t *)self->ptr)[nr] = sample;
		break;
	default:
		assert(!"sampler_write: bad bits");
	}
}

int sampler_init(struct sampler *sampler, const char *filename);
int sampler_close(struct sampler *sampler);

#endif

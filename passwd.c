#include <err.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "lsb.h"
#include "md5.h"
#include "passwd.h"
#include "util.h"

struct passwd {
	int initial_bits[32];

	struct sampler *sampler;

	struct stegger stegger;
};

static int bit_at_offset(const struct passwd *pwd, long offset, int bit)
{
	int add = offset / 4;
	int group = offset % 4;
	int bits[32];
	int i;

	for (i = 0; i < 32; i++)
		bits[i] = (pwd->initial_bits[i] + add) % 4;

	return bits[group * 8 + bit];
}

static int passwd_read(struct stegger *stegger, void *buf, size_t size, size_t offset)
{ 
	struct passwd *pwd = container_of(stegger, struct passwd, stegger);
	int i;
	unsigned char *bp = buf;
	sample_t sample;

	offset *= 8;

	if (offset + (size * 8) >= pwd->sampler->count) {
		warnx("passwd_read: bad offset");
		return -EINVAL;
	}

	for (i = 0; i < size; i++) {
		unsigned char b = 0;
		int bit;

		for (bit = 0; bit < 8; bit++) {
			int tbit;

			sample = sampler_read(pwd->sampler, offset);

			tbit = bit_at_offset(pwd, offset, bit);

			if (sample & (1 << tbit))
				b |= 1 << bit;

			offset++;
		}

		*bp++ = b;

	}

	return 0;
}

static int passwd_write(struct stegger *stegger, const void *buf, size_t size, size_t offset)
{
	struct passwd *pwd = container_of(stegger, struct passwd, stegger);
	const unsigned char *bp = buf;
	sample_t sample;
	int bit = 0;

	offset *= 8;

	if (offset + (size * 8) >= pwd->sampler->count) {
		warnx("passwd_read: bad offset");
		return -EINVAL;
	}

	for (;;) {
		int tbit;

		sample = sampler_read(pwd->sampler, offset);

		tbit = bit_at_offset(pwd, offset, bit);

		if ((*bp & (1 << bit)) != 0)
			sample |= (1 << tbit);
		else
			sample &= ~(1 << tbit);

		sampler_write(pwd->sampler, offset, sample);

		bit++;
		offset++;

		if (bit == 8) {
			bit = 0;
			if (size == 1)
				break;
			size--;
			bp++;
		}
	}

	return 0;
}

static int passwd_close(struct stegger *stegger)
{
	struct passwd *pwd = container_of(stegger, struct passwd, stegger);
	free(pwd);
	return 0;
}

int passwd_open(struct stegger **stegger, struct sampler *sampler, const char *password)
{
	struct passwd *pwd;
	MD5_CTX ctx;
	unsigned char md5[16];
	int i;

	pwd = malloc(sizeof(*pwd));
	if (!pwd)
		return -ENOMEM;

	MD5_Init(&ctx);
	MD5_Update(&ctx, password, strlen(password));
	MD5_Final(md5, &ctx);

	for (i = 0; i < 32; i++) {
		if (i % 2 == 0)
			pwd->initial_bits[i] = (md5[i/2] & 0xF) % 4;
		else
			pwd->initial_bits[i] = (md5[i/2] >> 4) % 4;
	}

	pwd->stegger.capacity = sampler->count / 8;
	pwd->stegger.read = passwd_read;
	pwd->stegger.write = passwd_write;
	pwd->stegger.close = passwd_close;

	pwd->sampler = sampler;

	*stegger = &pwd->stegger;

	return 0;
}

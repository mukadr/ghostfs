#include "steg.h"

struct wav {
	struct steg steg;
};

static ssize_t wav_read(struct steg *steg, void *buf, size_t size, size_t offset, int bits)
{ 
	struct wav *wav = container_of(steg, struct wav, steg);
}

static ssize_t wav_write(struct steg *steg, const void *buf, size_t size, size_t offset, int bits)
{
	struct wav *wav = container_of(steg, struct wav, steg);
}

static void wav_release(struct steg *steg)
{
	struct wav *wav = container_of(steg, struct wav, steg);
	free(wav);
}

static const struct steg_ops wav_ops = {
	.read = wav_read,
	.write = wav_write
	.release = wav_release
};

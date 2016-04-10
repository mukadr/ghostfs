#ifndef GHOST_STEGGER_H
#define GHOST_STEGGER_H

#include "sampler.h"
#include "util.h"

struct stegger {
	long capacity;

	int (*read)(struct stegger *stegger, void *buf, size_t size, size_t offset);
	int (*write)(struct stegger *stegger, const void *buf, size_t size, size_t offset);
	int (*close)(struct stegger *stegger);
};

static inline int stegger_read(struct stegger *stegger, void *buf, size_t size, size_t offset)
{
	return stegger->read(stegger, buf, size, offset);
}

static inline int stegger_write(struct stegger *stegger, const void *buf, size_t size, size_t offset)
{
	return stegger->write(stegger, buf, size, offset);
}

static inline int stegger_close(struct stegger *stegger)
{
	return stegger->close(stegger);
}

#endif

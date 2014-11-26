#include <err.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "fs.h"
#include "steg_wav.h"

enum {
	CLUSTER_SIZE = 4096,
};

struct ghostfs_header {
	uint16_t clusters;
} __attribute__((packed));

struct ghostfs {
	int status;
	struct ghostfs_header hdr;
	struct steg *steg;
};

struct cluster_header {
	uint16_t next;
	uint8_t used;
	uint8_t unused;
};

struct cluster {
	unsigned char data[4092];
	struct cluster_header hdr;
};

/*
 * Root directory '/' is stored at cluster 0.
 *
 * Each directory cluster have 66 entries(62 bytes each) = 4092bytes.
 * The remaining 4 bytes of the cluster is used to store the cluster_header
 *
 * An empty filename (filename[0] == '\0') means that the entry is empty
 */
struct dir_entry {
	char filename[56];
	uint32_t size; // highest bit is set when the entry is a directory
	uint16_t cluster;
} __attribute__((packed));

static inline int dir_entry_is_directory(const struct dir_entry *e)
{
	return (e->size & 0x80000000) != 0;
}

static inline uint32_t dir_entry_size(const struct dir_entry *e)
{
	return e->size & 0x7FFFFFFF;
}

// TODO: check if there is a filesystem (by doing MD5 checksum of the header and first cluster)
static void ghostfs_check(const struct ghostfs *gfs)
{
}

static int write_cluster(struct ghostfs *gfs, const struct cluster *cluster, int nr)
{
	size_t c0_offset = sizeof(struct ghostfs_header);

	return steg_write(gfs->steg, cluster, sizeof(struct cluster), c0_offset + nr*CLUSTER_SIZE, 1);
}

// create a new filesystem
int ghostfs_format(struct ghostfs *gfs)
{
	size_t avail;
	size_t clusters;
	struct cluster root;

	avail = steg_capacity(gfs->steg) - sizeof(struct ghostfs_header);
	clusters = avail / CLUSTER_SIZE;

	if (clusters < 1) {
		warnx("ghostfs: no minimum space available");
		return -1;
	}
	if (clusters > 0xFFFF)
		clusters = 0xFFFF;

	gfs->hdr.clusters = clusters;

	// write header
	if (steg_write(gfs->steg, &gfs->hdr, sizeof(struct ghostfs_header), 0, 1) < 0)
		return -1;

	// write first cluster (empty directory)
	memset(&root, 0, sizeof(root));
	if (write_cluster(gfs, &root, 0) < 0)
		return -1;

	return 0;
}

int ghostfs_status(const struct ghostfs *gfs)
{
	return gfs->status;
}

int ghostfs_open(struct ghostfs **pgfs, const char *filename)
{
	struct ghostfs *gfs;
	enum steg_type type;
	size_t len;

	len = strlen(filename);
	if (len < 5) {
		warnx("ghostfs: unknown file type");
		return -1;
	}

	if (memcmp(&filename[len-4], ".wav", 4) == 0) {
		type = STEG_WAV;
	} else {
		warnx("ghostfs: unknown file type");
		return -1;
	}

	gfs = malloc(sizeof(*gfs));
	if (!gfs) {
		warn("malloc");
		return -1;
	}

	switch (type) {
	case STEG_WAV:
		if (wav_open(&gfs->steg, filename) < 0) {
			free(gfs);
			return -1;
		}
		break;
	}

	if (steg_read(gfs->steg, &gfs->hdr, sizeof(struct ghostfs_header), 0, 1) < 0) {
		steg_close(gfs->steg);
		free(gfs);
		return -1;
	}

	ghostfs_check(gfs);

	*pgfs = gfs;
	return 0;
}

int ghostfs_close(struct ghostfs *gfs)
{
	int ret;
	ret = steg_close(gfs->steg);
	free(gfs);
	return ret;
}

int ghostfs_cluster_count(const struct ghostfs *gfs)
{
	return gfs->hdr.clusters;
}

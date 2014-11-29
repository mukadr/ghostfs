#include <err.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "fs.h"
#include "md5.h"
#include "steg.h"

enum {
	CLUSTER_SIZE = 4096,
};

// MD5(header+cluster0) | header | cluster0 .. clusterN

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

static int write_cluster(struct ghostfs *gfs, const struct cluster *cluster, int nr)
{
	size_t c0_offset = 16 + sizeof(struct ghostfs_header);

	return steg_write(gfs->steg, cluster, sizeof(struct cluster), c0_offset + nr*CLUSTER_SIZE, 1);
}

static int read_cluster(struct ghostfs *gfs, struct cluster *cluster, int nr)
{
	size_t c0_offset = 16 + sizeof(struct ghostfs_header);

	return steg_read(gfs->steg, cluster, sizeof(struct cluster), c0_offset + nr*CLUSTER_SIZE, 1);
}

static void ghostfs_check(struct ghostfs *gfs)
{
	MD5_CTX md5_ctx;
	unsigned char md5_fs[16];
	unsigned char md5[16];
	struct cluster root;

	gfs->status = GHOSTFS_UNFORMATTED;

	if (steg_read(gfs->steg, &gfs->hdr, sizeof(struct ghostfs_header), 16, 1) < 0)
		return;

	if (steg_read(gfs->steg, md5_fs, sizeof(md5_fs), 0, 1) < 0)
		return;

	if (read_cluster(gfs, &root, 0) < 0)
		return;

	MD5_Init(&md5_ctx);
	MD5_Update(&md5_ctx, &gfs->hdr, sizeof(gfs->hdr));
	MD5_Update(&md5_ctx, &root, sizeof(root));
	MD5_Final(md5, &md5_ctx);

	if (memcmp(md5, md5_fs, 16) != 0)
		return;

	gfs->status = GHOSTFS_OK;
}

// create a new filesystem
int ghostfs_format(struct ghostfs *gfs)
{
	size_t avail;
	size_t clusters;
	struct cluster root;
	MD5_CTX md5_ctx;
	unsigned char md5[16];

	avail = steg_capacity(gfs->steg) - 16 - sizeof(struct ghostfs_header);
	clusters = avail / CLUSTER_SIZE;

	if (clusters < 1) {
		warnx("fs: no minimum space available");
		return -1;
	}
	if (clusters > 0xFFFF)
		clusters = 0xFFFF;

	gfs->hdr.clusters = clusters;
	memset(&root, 0, sizeof(root));

	MD5_Init(&md5_ctx);
	MD5_Update(&md5_ctx, &gfs->hdr, sizeof(gfs->hdr));
	MD5_Update(&md5_ctx, &root, sizeof(root));
	MD5_Final(md5, &md5_ctx);

	// write md5 of header+root
	if (steg_write(gfs->steg, md5, sizeof(md5), 0, 1) < 0)
		return -1;

	// write header
	if (steg_write(gfs->steg, &gfs->hdr, sizeof(gfs->hdr), 16, 1) < 0)
		return -1;

	// write first cluster (empty directory)
	if (steg_write(gfs->steg, &root, sizeof(root), 16 + sizeof(struct ghostfs_header), 1) < 0)
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

	gfs = malloc(sizeof(*gfs));
	if (!gfs) {
		warn("fs: malloc");
		return -1;
	}

	if (steg_open(&gfs->steg, filename) < 0) {
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

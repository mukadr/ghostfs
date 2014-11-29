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
	struct cluster **clusters; // cluster cache
};

struct cluster_header {
	uint16_t next;
	uint8_t used;
	uint8_t dirty; // unused byte. we use it only in-memory to know if the cache entry is dirty
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

static struct cluster *get_cluster(struct ghostfs *gfs, int nr)
{
	if (!gfs->clusters[nr]) {
		gfs->clusters[nr] = malloc(CLUSTER_SIZE);
		if (!gfs->clusters[nr]) {
			warn("fs: malloc");
			return NULL;
		}
	}
	return gfs->clusters[nr];
}

static inline void cluster_set_dirty(struct cluster *cluster)
{
	cluster->hdr.dirty = 1;
}

static int cluster_write(struct ghostfs *gfs, int nr)
{
	const size_t c0_offset = 16 + sizeof(struct ghostfs_header);
	struct cluster *cluster;
	int ret;

	cluster = get_cluster(gfs, nr);
	if (!cluster)
		return -1;

	ret = steg_write(gfs->steg, cluster, CLUSTER_SIZE, c0_offset + nr*CLUSTER_SIZE, 1);
	if (ret < 0)
		return ret;
	if (ret != CLUSTER_SIZE)
		return 0;

	cluster->hdr.dirty = 0;
	return 1;
}

static int cluster_read(struct ghostfs *gfs, int nr, struct cluster **pcluster)
{
	const size_t c0_offset = 16 + sizeof(struct ghostfs_header);
	struct cluster *cluster;
	int ret;

	cluster = get_cluster(gfs, nr);
	if (!cluster)
		return -1;

	*pcluster = cluster;

	ret = steg_read(gfs->steg, cluster, CLUSTER_SIZE, c0_offset + nr*CLUSTER_SIZE, 1);
	if (ret < 0)
		return ret;
	if (ret != CLUSTER_SIZE)
		return 0;

	cluster->hdr.dirty = 0;
	return 1;
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

	if (steg_read(gfs->steg, &root, sizeof(root), 16 + sizeof(struct ghostfs_header), 1) < 0)
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

	gfs = calloc(1, sizeof(*gfs));
	if (!gfs) {
		warn("fs: malloc");
		return -1;
	}
	*pgfs = gfs;

	if (steg_open(&gfs->steg, filename) < 0) {
		free(gfs);
		return -1;
	}

	ghostfs_check(gfs);
	if (gfs->status != GHOSTFS_OK) // if there is no file system, we stop here
		return 0;

	// allocate cluster cache
	gfs->clusters = calloc(1, sizeof(struct cluster *) * gfs->hdr.clusters);
	if (!gfs->clusters) {
		warn("fs: malloc");
		steg_close(gfs->steg);
		free(gfs);
		return -1;
	}

	return 0;
}

int ghostfs_close(struct ghostfs *gfs)
{
	int ret;
	int i;

	ret = steg_close(gfs->steg);
	for (i = 0; i < gfs->hdr.clusters; i++)
		free(gfs->clusters[i]);
	free(gfs->clusters);
	free(gfs);
	return ret;
}

int ghostfs_cluster_count(const struct ghostfs *gfs)
{
	return gfs->hdr.clusters;
}

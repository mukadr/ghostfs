#include <assert.h>
#include <err.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "fs.h"
#include "md5.h"
#include "steg.h"

enum {
	CLUSTER_SIZE = 4096,
	DIR_ENTRIES_PER_CLUSTER = 66,
	FILENAME_SIZE = 56
};

// MD5(header+cluster0) | header | cluster0 .. clusterN

struct ghostfs_header {
	uint16_t clusters;
} __attribute__((packed));

/*
 * Root directory '/' is stored at cluster 0.
 *
 * Each directory cluster have 66 entries(62 bytes each) = 4092bytes.
 * The remaining 4 bytes of the cluster are used to store the cluster_header
 *
 * An empty filename (filename[0] == '\0') means that the entry is empty
 */
struct dir_entry {
	char filename[FILENAME_SIZE];
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

static inline int dir_entry_used(const struct dir_entry *e)
{
	return e->filename[0] != '\0';
}

struct ghostfs {
	int status;
	struct ghostfs_header hdr;
	struct steg *steg;
	struct cluster **clusters; // cluster cache
	struct dir_entry root_entry;
};

struct cluster_header {
	uint16_t next;
	uint8_t used;
	uint8_t dirty; // unused byte. we use it only in-memory to know if the cache entry is dirty
} __attribute__((packed));

struct cluster {
	unsigned char data[4092];
	struct cluster_header hdr;
} __attribute__((packed));

static inline void cluster_set_dirty(struct cluster *cluster, int dirty)
{
	cluster->hdr.dirty = dirty;
}

static inline int cluster_is_dirty(const struct cluster *cluster)
{
	return cluster->hdr.dirty;
}

static int cluster_get(struct ghostfs *gfs, int nr, struct cluster **pcluster);
static int write_cluster(struct ghostfs *gfs, struct cluster *cluster, int nr);
static int read_cluster(struct ghostfs *gfs, struct cluster *cluster, int nr);
static void ghostfs_check(struct ghostfs *gfs);

struct dir_iter {
	struct ghostfs *gfs;
	struct cluster *cluster;
	struct dir_entry *entry;
	int entry_nr; // to make the code simpler
};

static int dir_iter_init(struct ghostfs *gfs, struct dir_iter *it, int cluster_nr)
{
	if (cluster_get(gfs, cluster_nr, &it->cluster) < 0)
		return -1;

	it->gfs = gfs;
	it->entry = (struct dir_entry *)it->cluster->data;
	it->entry_nr = 0;
	return 0;
}

static int dir_iter_next(struct dir_iter *it)
{
	if (it->entry_nr >= DIR_ENTRIES_PER_CLUSTER - 1) {
		struct cluster *c;

		if (it->cluster->hdr.next == 0 || cluster_get(it->gfs, it->cluster->hdr.next, &c) < 0)
			return 0;

		it->cluster = c;
		it->entry_nr = 0;
		it->entry = (struct dir_entry *)it->cluster->data;
		return 1;
	}

	it->entry_nr++;
	it->entry++;
	return 1;
}

static int dir_iter_next_used(struct dir_iter *it)
{
	struct dir_iter temp = *it;

	do {
		if (!dir_iter_next(&temp))
			return 0;
	} while (!dir_entry_used(it->entry));

	*it = temp;
	return 1;
}

static int component_eq(const char *comp, const char *name)
{
	while (*comp && *name && *comp != '/') {
		comp++;
		name++;
	}

	return (!*comp || *comp == '/') && !*name;
}

static int dir_iter_lookup(struct ghostfs *gfs, struct dir_iter *it, const char *path, int skip_last)
{
	const char *comp;

	if (!path[0]) {
		warnx("fs: dir_iter_lookup: empty path");
		return 0;
	}

	if (dir_iter_init(gfs, it, 0) < 0)
		return 0;

	comp = path + 1; // skip first slash
	if (!comp[0] || (skip_last && !strchr(comp, '/'))) {
		it->entry = &gfs->root_entry;
		return 1;
	}

	for (;;) {
		if (component_eq(comp, it->entry->filename)) {
			const char *next = strchr(comp, '/');

			if (!next[0] || (skip_last && !strchr(next + 1, '/'))) // finished
				return 1;
			if (!dir_entry_is_directory(it->entry)) // not a valid path
				return 0;
			// start searching in child directory
			if (dir_iter_init(gfs, it, it->entry->cluster) < 0)
				return 0;
			comp = next + 1;
			continue;
		}
		if (!dir_iter_next_used(it)) // nothing more to search
			return 0;
	}
	// unreachable
}

static const char *last_component(const char *path)
{
	const char *s;

	while ((s = strchr(path, '/')) != NULL)
		path = s + 1;

	return path;
}

static struct dir_entry *find_empty_entry(struct ghostfs *gfs, int cluster_nr)
{
	struct dir_iter it;

	if (dir_iter_init(gfs, &it, cluster_nr) < 0)
		return NULL;

	while (dir_entry_used(it.entry))
		if (!dir_iter_next(&it))
			return NULL;

	return it.entry;
}

static int dir_contains(struct ghostfs *gfs, int cluster_nr, const char *name)
{
	struct dir_iter it;

	if (dir_iter_init(gfs, &it, cluster_nr) < 0)
		return 0;

	do {
		if (!strcmp(it.entry->filename, name))
			return 1;
	} while (dir_iter_next_used(&it));

	return 0;
}

// only supports regular files
int ghostfs_create(struct ghostfs *gfs, const char *path)
{
	struct dir_iter it;
	const char *name;
	struct dir_entry *entry;

	if (!dir_iter_lookup(gfs, &it, path, 1)) {
		warnx("fs: ghostfs_mknod: invalid path");
		return -1;
	}

	if (!dir_entry_is_directory(it.entry)) {
		warnx("fs: ghostfs_mknod: invalid path");
		return -1;
	}

	name = last_component(path);

	if (dir_contains(gfs, it.entry->cluster, name)) {
		warnx("fs: ghostfs_mknod: file exists");
		return -1;
	}

	entry = find_empty_entry(gfs, it.entry->cluster);
	if (!entry) { // cluster is full
		// TODO: expand number of clusters
		return 0;
	}

	strncpy(entry->filename, name, FILENAME_SIZE);
	entry->filename[FILENAME_SIZE - 1] = '\0';
	entry->cluster = 0;
	entry->size = 0;
	return 1;
}

static int cluster_get(struct ghostfs *gfs, int nr, struct cluster **pcluster)
{
	if (nr >= gfs->hdr.clusters) {
		warnx("fs: invalid cluster number %d", nr);
		return -1;
	}
	if (!gfs->clusters[nr]) {
		struct cluster *c = malloc(CLUSTER_SIZE);
		if (!c) {
			warn("fs: malloc");
			return -1;
		}
		if (read_cluster(gfs, c, nr) < 0) {
			free(c);
			return -1;
		}
		gfs->clusters[nr] = c;
	}
	*pcluster = gfs->clusters[nr];
	return 0;
}

static int write_cluster(struct ghostfs *gfs, struct cluster *cluster, int nr)
{
	const size_t c0_offset = 16 + sizeof(struct ghostfs_header);
	int ret;

	ret = steg_write(gfs->steg, cluster, CLUSTER_SIZE, c0_offset + nr*CLUSTER_SIZE, 1);
	if (ret < 0)
		return ret;
	if (ret != CLUSTER_SIZE)
		return -1;

	cluster_set_dirty(cluster, 0);
	return 0;
}

static int read_cluster(struct ghostfs *gfs, struct cluster *cluster, int nr)
{
	const size_t c0_offset = 16 + sizeof(struct ghostfs_header);
	int ret;

	ret = steg_read(gfs->steg, cluster, CLUSTER_SIZE, c0_offset + nr*CLUSTER_SIZE, 1);
	if (ret < 0)
		return ret;
	if (ret != CLUSTER_SIZE)
		return -1;

	cluster_set_dirty(cluster, 0);
	return 0;
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

static int write_header(struct ghostfs *gfs, const struct cluster *cluster0)
{
	MD5_CTX md5_ctx;
	unsigned char md5[16];

	MD5_Init(&md5_ctx);
	MD5_Update(&md5_ctx, &gfs->hdr, sizeof(gfs->hdr));
	MD5_Update(&md5_ctx, cluster0, sizeof(*cluster0));
	MD5_Final(md5, &md5_ctx);

	// write md5 of header+root
	if (steg_write(gfs->steg, md5, sizeof(md5), 0, 1) < 0)
		return -1;

	// write header
	if (steg_write(gfs->steg, &gfs->hdr, sizeof(gfs->hdr), 16, 1) < 0)
		return -1;

	// write first cluster
	if (steg_write(gfs->steg, cluster0, sizeof(*cluster0), 16 + sizeof(struct ghostfs_header), 1) < 0)
		return -1;

	return 0;
}

// create a new filesystem
int ghostfs_format(struct ghostfs *gfs)
{
	size_t avail;
	size_t clusters;
	struct cluster root;

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

	if (write_header(gfs, &root) < 0)
		return -1;

	return 0;
}

int ghostfs_status(const struct ghostfs *gfs)
{
	return gfs->status;
}

static void print_dir_entries(struct ghostfs *gfs)
{
	struct dir_iter it;

	if (dir_iter_init(gfs, &it, 0) < 0)
		return;

	do {
		if (dir_entry_used(it.entry))
			printf("'%s'\n", it.entry->filename);
	} while (dir_iter_next_used(&it));
}

int ghostfs_mount(struct ghostfs **pgfs, const char *filename)
{
	struct ghostfs *gfs;

	gfs = calloc(1, sizeof(*gfs));
	if (!gfs) {
		warn("fs: malloc");
		return -1;
	}
	*pgfs = gfs;

	gfs->root_entry.size = 0x80000000;

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
		ghostfs_umount(gfs);
		return -1;
	}

	print_dir_entries(gfs);

	return 0;
}

int ghostfs_sync(struct ghostfs *gfs)
{
	struct cluster *c0;
	int ret, i;

	ret = cluster_get(gfs, 0, &c0);
	if (ret < 0)
		return ret;

	ret = write_header(gfs, c0);
	if (ret < 0)
		return ret;

	if (!gfs->clusters) // nothing more to sync
		return 0;

	for (i = 1; i < gfs->hdr.clusters; i++) {
		struct cluster *c = gfs->clusters[i];

		if (!c || !cluster_is_dirty(c))
			continue;

		ret = write_cluster(gfs, c, i);
		if (ret < 0)
			return ret;

		cluster_set_dirty(c, 0);
	}

	return 0;
}

int ghostfs_umount(struct ghostfs *gfs)
{
	int ret, ret_close;
	int i;

	ret = ghostfs_sync(gfs);
	ret_close = steg_close(gfs->steg);

	if (gfs->clusters) {
		for (i = 0; i < gfs->hdr.clusters; i++)
			free(gfs->clusters[i]);
		free(gfs->clusters);
	}
	free(gfs);

	return ret == 0 ? ret_close : ret;
}

int ghostfs_cluster_count(const struct ghostfs *gfs)
{
	return gfs->hdr.clusters;
}

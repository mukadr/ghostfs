#include <err.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "fs.h"
#include "md5.h"
#include "steg.h"

enum {
	CLUSTER_SIZE = 4096,
	CLUSTER_DIRS = 66,
	FILENAME_SIZE = 56
};

// MD5(header+cluster0) | header | cluster0 .. clusterN

struct ghostfs_header {
	uint16_t cluster_count;
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

static inline bool dir_entry_is_directory(const struct dir_entry *e)
{
	return (e->size & 0x80000000) != 0;
}

static inline uint32_t dir_entry_size(const struct dir_entry *e)
{
	return e->size & 0x7FFFFFFF;
}

static inline bool dir_entry_used(const struct dir_entry *e)
{
	return e->filename[0] != '\0';
}

struct ghostfs {
	struct ghostfs_header hdr;
	struct steg *steg;
	struct cluster **clusters;
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

static inline void cluster_set_dirty(struct cluster *cluster, bool dirty)
{
	cluster->hdr.dirty = dirty ? 1 : 0;
}

static inline bool cluster_dirty(const struct cluster *cluster)
{
	return cluster->hdr.dirty != 0;
}

static int cluster_get(struct ghostfs *gfs, int nr, struct cluster **pcluster);
static int write_cluster(struct ghostfs *gfs, struct cluster *cluster, int nr);
static int read_cluster(struct ghostfs *gfs, struct cluster *cluster, int nr);
static int ghostfs_check(struct ghostfs *gfs);
static int ghostfs_free(struct ghostfs *gfs);

struct dir_iter {
	struct ghostfs *gfs;
	struct cluster *cluster;
	struct dir_entry *entry;
	int entry_nr;
};

static int dir_iter_init(struct ghostfs *gfs, struct dir_iter *it, int cluster_nr)
{
	int ret;

	ret = cluster_get(gfs, cluster_nr, &it->cluster);
	if (ret < 0)
		return ret;

	it->gfs = gfs;
	it->entry = (struct dir_entry *)it->cluster->data;
	it->entry_nr = 0;
	return 0;
}

static int dir_iter_next(struct dir_iter *it)
{
	int ret;

	if (it->entry_nr >= CLUSTER_DIRS - 1) {
		struct cluster *c;

		if (it->cluster->hdr.next == 0)
			return 0;

		ret = cluster_get(it->gfs, it->cluster->hdr.next, &c);
		if (ret < 0)
			return ret;

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
	int ret;

	do {
		ret = dir_iter_next(&temp);
		if (ret <= 0)
			return ret;
	} while (!dir_entry_used(it->entry));

	*it = temp;
	return 1;
}

static bool component_eq(const char *comp, const char *name, size_t n)
{
	while (n > 0 && *comp && *comp != '/' && *comp == *name) {
		comp++;
		name++;
		n--;
	}
	if (n == 0)
		return false;

	return (!*comp || *comp == '/') && !*name;
}

static int dir_iter_lookup(struct ghostfs *gfs, struct dir_iter *it, const char *path, int skip_last)
{
	const char *comp;
	int ret;

	if (!path[0])
		return -EINVAL;

	ret = dir_iter_init(gfs, it, 0);
	if (ret < 0)
		return ret;

	comp = path + 1;
	if (!comp[0] || (skip_last && !strchr(comp, '/'))) {
		it->entry = &gfs->root_entry;
		return 0;
	}

	for (;;) {
		if (component_eq(comp, it->entry->filename, FILENAME_SIZE)) {
			const char *next = strchr(comp, '/');

			// finished
			if (!next || (skip_last && !strchr(next + 1, '/')))
				return 0;

			if (!dir_entry_is_directory(it->entry))
				return -ENOTDIR;

			// start searching child directory
			ret = dir_iter_init(gfs, it, it->entry->cluster);
			if (ret < 0)
				return ret;

			comp = next + 1;
			continue;
		}

		ret = dir_iter_next_used(it);
		if (ret < 0)
			return ret;
		if (!ret)
			break;
	}

	return -ENOENT;
}

static const char *last_component(const char *path)
{
	const char *s;

	while ((s = strchr(path, '/')) != NULL)
		path = s + 1;

	return path;
}

static int find_empty_entry(struct ghostfs *gfs, struct dir_iter *iter, int cluster_nr)
{
	struct dir_iter it;
	int ret;

	ret = dir_iter_init(gfs, &it, cluster_nr);
	if (ret < 0)
		return ret;

	while (dir_entry_used(it.entry)) {
		ret = dir_iter_next(&it);
		if (ret < 0)
			return ret;
		if (!ret) {
			ret = -ENOSPC;
			break;
		}
	}

	*iter = it;
	return ret;
}

static int dir_contains(struct ghostfs *gfs, int cluster_nr, const char *name)
{
	struct dir_iter it;
	int ret;

	ret = dir_iter_init(gfs, &it, cluster_nr);
	if (ret < 0)
		return ret;

	for (;;) {
		if (!strncmp(it.entry->filename, name, FILENAME_SIZE))
			return 1;

		ret = dir_iter_next_used(&it);
		if (ret <= 0)
			break;
	}

	return ret;
}

static int find_free_cluster(struct ghostfs *gfs, struct cluster **pcluster)
{
	struct cluster *c;
	int i;

	for (i = 1; i < gfs->hdr.cluster_count; i++) {
		int nr;

		nr = cluster_get(gfs, i, &c);
		if (nr < 0)
			return nr;

		if (!c->hdr.used) {
			if (pcluster)
				*pcluster = c;
			return i;
		}
	}

	return -ENOSPC;
}

static int alloc_cluster(struct ghostfs *gfs, struct cluster **pcluster)
{
	struct cluster *c;
	int nr;

	nr = find_free_cluster(gfs, &c);
	if (nr < 0)
		return nr;

	memset(c, 0, sizeof(*c));
	c->hdr.used = 1;
	cluster_set_dirty(c, true);

	if (pcluster)
		*pcluster = c;

	return nr;
}

static void free_cluster(struct cluster *c)
{
	c->hdr.used = 0;
	cluster_set_dirty(c, true);
}

static int create_entry(struct ghostfs *gfs, const char *path, bool is_dir)
{
	struct dir_iter it;
	struct dir_entry *entry;
	struct cluster *prev = NULL, *next;
	const char *name;
	int cluster_nr = 0;
	int ret;

	ret = dir_iter_lookup(gfs, &it, path, true);
	if (ret < 0)
		return ret;

	if (!dir_entry_is_directory(it.entry))
		return -ENOTDIR;

	name = last_component(path);
	if (strlen(name) > FILENAME_SIZE - 1)
		return -ENAMETOOLONG;

	if (dir_contains(gfs, it.entry->cluster, name))
		return -EEXIST;

	ret = find_empty_entry(gfs, &it, it.entry->cluster);
	if (ret < 0) {
		int nr;

		if (ret != -ENOSPC)
			return ret;

		nr = alloc_cluster(gfs, &next);
		if (nr < 0)
			return nr;

		prev = it.cluster;
		ret = find_empty_entry(gfs, &it, nr);
		if (ret < 0) { // should never happen
			warnx("BUG: failed to find empty entry in empty cluster");
			free_cluster(next);
			return ret;
		}

		prev->hdr.next = nr;
		cluster_set_dirty(prev, true);
	}

	if (is_dir) {
		cluster_nr = alloc_cluster(gfs, NULL);
		if (cluster_nr < 0) {
			prev->hdr.next = 0;
			cluster_set_dirty(prev, false);
			free_cluster(next);
			return cluster_nr;
		}
	}

	entry = it.entry;

	strncpy(entry->filename, name, FILENAME_SIZE);
	entry->filename[FILENAME_SIZE - 1] = '\0';
	entry->size = is_dir ? 0x80000000 : 0;
	entry->cluster = cluster_nr;

	cluster_set_dirty(it.cluster, true);

	return 0;
}

int ghostfs_create(struct ghostfs *gfs, const char *path)
{
	return create_entry(gfs, path, 0);
}

int ghostfs_mkdir(struct ghostfs *gfs, const char *path)
{
	return create_entry(gfs, path, 1);
}

static int cluster_get(struct ghostfs *gfs, int nr, struct cluster **pcluster)
{
	int ret;

	if (nr >= gfs->hdr.cluster_count) {
		warnx("fs: invalid cluster number %d", nr);
		return -ERANGE;
	}

	if (!gfs->clusters[nr]) {
		struct cluster *c;

		c = malloc(CLUSTER_SIZE);
		if (!c)
			return -ENOMEM;

		ret = read_cluster(gfs, c, nr);
		if (ret < 0) {
			free(c);
			return ret;
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

	cluster_set_dirty(cluster, false);
	return 0;
}

static int read_cluster(struct ghostfs *gfs, struct cluster *cluster, int nr)
{
	const size_t c0_offset = 16 + sizeof(struct ghostfs_header);
	int ret;

	ret = steg_read(gfs->steg, cluster, CLUSTER_SIZE, c0_offset + nr*CLUSTER_SIZE, 1);
	if (ret < 0)
		return ret;

	cluster_set_dirty(cluster, false);
	return 0;
}

static int ghostfs_check(struct ghostfs *gfs)
{
	MD5_CTX md5_ctx;
	unsigned char md5_fs[16];
	unsigned char md5[16];
	struct cluster root;
	int ret;

	ret = steg_read(gfs->steg, &gfs->hdr, sizeof(struct ghostfs_header), 16, 1);
	if (ret < 0)
		return ret;

	ret = steg_read(gfs->steg, md5_fs, sizeof(md5_fs), 0, 1);
	if (ret < 0)
		return ret;

	ret = steg_read(gfs->steg, &root, sizeof(root), 16 + sizeof(struct ghostfs_header), 1);
	if (ret < 0)
		return ret;

	MD5_Init(&md5_ctx);
	MD5_Update(&md5_ctx, &gfs->hdr, sizeof(gfs->hdr));
	MD5_Update(&md5_ctx, &root, sizeof(root));
	MD5_Final(md5, &md5_ctx);

	if (memcmp(md5, md5_fs, 16) != 0) {
		warnx("fs: incorrect checksum");
		return -EMEDIUMTYPE;
	}

	return 0;
}

static int write_header(struct ghostfs *gfs, struct cluster *cluster0)
{
	MD5_CTX md5_ctx;
	unsigned char md5[16];
	int ret;

	MD5_Init(&md5_ctx);
	MD5_Update(&md5_ctx, &gfs->hdr, sizeof(gfs->hdr));
	MD5_Update(&md5_ctx, cluster0, sizeof(*cluster0));
	MD5_Final(md5, &md5_ctx);

	// write md5 of header+root
	ret = steg_write(gfs->steg, md5, sizeof(md5), 0, 1);
	if (ret < 0)
		return ret;

	// write header
	ret = steg_write(gfs->steg, &gfs->hdr, sizeof(gfs->hdr), 16, 1);
	if (ret < 0)
		return ret;

	// write first cluster
	ret = write_cluster(gfs, cluster0, 0);
	if (ret < 0)
		return ret;

	return 0;
}

// create a new filesystem
int ghostfs_format(const char *filename)
{
	struct ghostfs gfs;
	size_t avail;
	size_t count;
	struct cluster cluster;
	int ret, i;

	ret = steg_open(&gfs.steg, filename);
	if (ret < 0)
		return ret;

	avail = steg_capacity(gfs.steg) - 16 - sizeof(struct ghostfs_header);
	count = avail / CLUSTER_SIZE;

	if (count < 1) {
		steg_close(gfs.steg);
		return -ENOSPC;
	}
	if (count > 0xFFFF) {
		warnx("fs: %lu clusters available, using only %d", count, 0xFFFF);
		count = 0xFFFF;
	}

	gfs.hdr.cluster_count = count;
	memset(&cluster, 0, sizeof(cluster));

	ret = write_header(&gfs, &cluster);
	if (ret < 0) {
		steg_close(gfs.steg);
		return ret;
	}

	for (i = 1; i < count; i++) {
		ret = write_cluster(&gfs, &cluster, i);
		if (ret < 0) {
			steg_close(gfs.steg);
			return ret;
		}
	}

	return steg_close(gfs.steg);
}

static void print_dir_entries(struct ghostfs *gfs, int cluster_nr, const char *parent)
{
	struct dir_iter it;
	char buf[256] = "";

	if (dir_iter_init(gfs, &it, cluster_nr) < 0)
		return;

	do {
		if (dir_entry_used(it.entry)) {
			snprintf(buf, sizeof(buf), "%s/%s", parent, it.entry->filename);
			printf("%s", buf);
			if (dir_entry_is_directory(it.entry)) {
				printf("/\n");
				print_dir_entries(gfs, it.entry->cluster, buf);
			} else {
				printf("\n");
			}
		}
	} while (dir_iter_next_used(&it));
}

static void print_filesystem(struct ghostfs *gfs)
{
	print_dir_entries(gfs, 0, "");
}

int ghostfs_mount(struct ghostfs **pgfs, const char *filename)
{
	struct ghostfs *gfs;
	int ret;

	gfs = calloc(1, sizeof(*gfs));
	if (!gfs)
		return -ENOMEM;

	*pgfs = gfs;

	gfs->root_entry.size = 0x80000000;

	ret = steg_open(&gfs->steg, filename);
	if (ret < 0) {
		free(gfs);
		return ret;
	}

	ret = ghostfs_check(gfs);
	if (ret < 0) {
		ghostfs_free(gfs);
		return ret;
	}

	gfs->clusters = calloc(1, sizeof(struct cluster *) * gfs->hdr.cluster_count);
	if (!gfs->clusters) {
		ghostfs_free(gfs);
		return -ENOMEM;
	}

	print_filesystem(gfs);

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

	for (i = 1; i < gfs->hdr.cluster_count; i++) {
		struct cluster *c = gfs->clusters[i];

		if (!c || !cluster_dirty(c))
			continue;

		ret = write_cluster(gfs, c, i);
		if (ret < 0)
			return ret;

		cluster_set_dirty(c, false);
	}

	return 0;
}

static int ghostfs_free(struct ghostfs *gfs)
{
	int ret;

	ret = steg_close(gfs->steg);

	if (gfs->clusters) {
		int i;

		for (i = 0; i < gfs->hdr.cluster_count; i++)
			free(gfs->clusters[i]);

		free(gfs->clusters);
	}

	free(gfs);

	return ret;
}

int ghostfs_umount(struct ghostfs *gfs)
{
	int ret;

	ret = ghostfs_sync(gfs);
	if (ret < 0) {
		ghostfs_free(gfs);
		return ret;
	}
	return ghostfs_free(gfs);
}

int ghostfs_cluster_count(const struct ghostfs *gfs)
{
	return gfs->hdr.cluster_count;
}

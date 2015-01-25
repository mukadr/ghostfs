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
	CLUSTER_DATA = 4092,
	CLUSTER_DIRS = 66,
	FILENAME_SIZE = 56,
	FILESIZE_MAX = 0x7FFFFFFF
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

static inline void dir_entry_set_size(struct dir_entry *e, uint32_t new_size, bool is_dir)
{
	e->size = new_size & 0x7FFFFFFF;
	if (is_dir)
		e->size |= 0x80000000;
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
	unsigned char data[CLUSTER_DATA];
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

struct ghostfs_entry {
	struct dir_iter it;
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
			return -ENOENT;

		ret = cluster_get(it->gfs, it->cluster->hdr.next, &c);
		if (ret < 0)
			return ret;

		it->cluster = c;
		it->entry_nr = 0;
		it->entry = (struct dir_entry *)it->cluster->data;
		return 0;
	}

	it->entry_nr++;
	it->entry++;
	return 0;
}

static int dir_iter_next_used(struct dir_iter *it)
{
	struct dir_iter temp = *it;
	int ret;

	do {
		ret = dir_iter_next(&temp);
		if (ret < 0)
			return ret;
	} while (!dir_entry_used(temp.entry));

	*it = temp;
	return 0;
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

static int dir_iter_lookup(struct ghostfs *gfs, struct dir_iter *it, const char *path, bool skip_last)
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
			break;
	}

	return ret;
}

static const char *last_component(const char *path)
{
	const char *s;

	while ((s = strchr(path, '/')) != NULL)
		path = s + 1;

	return path;
}

/*
 * Updates iter to point to the first unused entry in the cluster.
 * If no entry is available, iter is updated to the last entry and -ENOENT is returned.
 */
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
			break;
	}

	if (ret == 0 || ret == -ENOENT)
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
			return 0;

		ret = dir_iter_next_used(&it);
		if (ret < 0)
			break;
	}

	return ret;
}

// allocates a list of clusters
static int alloc_clusters(struct ghostfs *gfs, int count, struct cluster **pfirst, bool zero)
{
	struct cluster *prev = NULL;
	struct cluster *c;
	int first = 0;
	int pos = 1;
	int alloc = 0;
	int ret;

	while (alloc < count) {
		for (;;) {
			if (pos >= gfs->hdr.cluster_count) {
				ret = -ENOSPC;
				goto undo;
			}

			ret = cluster_get(gfs, pos, &c);
			if (ret < 0)
				goto undo;

			if (!c->hdr.used) {
				if (zero)
					memset(c->data, 0, sizeof(c->data));

				c->hdr.used = 1;
				cluster_set_dirty(c, true);

				if (!first) {
					first = pos;
					if (pfirst)
						*pfirst = c;
				} else {
					prev->hdr.next = pos;
				}
				prev = c;
				pos++;
				break;
			}
			pos++;
		}
		alloc++;
	}

	prev->hdr.next = 0;

	return first;
undo:
	pos = first;

	while (alloc > 0) {
		int r = cluster_get(gfs, pos, &c);
		if (r < 0)
			return r;

		c->hdr.used = 0;
		cluster_set_dirty(c, true);

		pos = c->hdr.next;
		alloc--;
	}

	return ret;
}

static int free_clusters(struct ghostfs *gfs, struct cluster *c)
{
	int ret;

	for (;;) {
		c->hdr.used = 0;
		cluster_set_dirty(c, true);

		if (!c->hdr.next)
			break;

		ret = cluster_get(gfs, c->hdr.next, &c);
		if (ret < 0) {
			errno = -ret;
			warn("failed to free cluster");
			return ret;
		}
	}

	return 0;
}

static int create_entry(struct ghostfs *gfs, const char *path, bool is_dir)
{
	struct dir_iter it;
	struct cluster *prev = NULL, *next = NULL;
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

	if (dir_contains(gfs, it.entry->cluster, name) == 0)
		return -EEXIST;

	ret = find_empty_entry(gfs, &it, it.entry->cluster);
	if (ret < 0) {
		int nr;

		if (ret != -ENOENT)
			return ret;

		nr = alloc_clusters(gfs, 1, &next, true);
		if (nr < 0)
			return nr;

		prev = it.cluster;
		ret = find_empty_entry(gfs, &it, nr);
		if (ret < 0) { // should never happen
			warnx("BUG: failed to find empty entry in empty cluster");
			free_clusters(gfs, next);
			return ret;
		}

		prev->hdr.next = nr;
		cluster_set_dirty(prev, true);
	}

	if (is_dir) {
		cluster_nr = alloc_clusters(gfs, 1, NULL, true);
		if (cluster_nr < 0) {
			if (next) {
				free_clusters(gfs, next);
				prev->hdr.next = 0;
			}
			return cluster_nr;
		}
	}

	strcpy(it.entry->filename, name);
	dir_entry_set_size(it.entry, 0, is_dir);
	it.entry->cluster = cluster_nr;
	cluster_set_dirty(it.cluster, true);

	return 0;
}

int ghostfs_create(struct ghostfs *gfs, const char *path)
{
	return create_entry(gfs, path, false);
}

int ghostfs_mkdir(struct ghostfs *gfs, const char *path)
{
	return create_entry(gfs, path, true);
}

static int remove_entry(struct ghostfs *gfs, const char *path, bool is_dir)
{
	struct dir_iter link, it;
	int ret;

	ret = dir_iter_lookup(gfs, &link, path, false);
	if (ret < 0)
		return ret;

	if (is_dir != dir_entry_is_directory(link.entry))
		return is_dir ? -ENOTDIR : -EISDIR;

	// no clusters, we are done
	if (!link.entry->cluster)
		goto unlink;

	ret = dir_iter_init(gfs, &it, link.entry->cluster);
	if (ret < 0)
		return ret;

	// make sure directory is empty
	if (is_dir) {
		if (dir_entry_used(it.entry))
			return -ENOTEMPTY;

		ret = dir_iter_next_used(&it);
		if (ret != -ENOENT)
			return ret == 0 ? -ENOTEMPTY : ret;
	}

	free_clusters(gfs, it.cluster);
unlink:
	link.entry->filename[0] = '\0';
	cluster_set_dirty(link.cluster, true);

	return 0;
}

int ghostfs_unlink(struct ghostfs *gfs, const char *path)
{
	return remove_entry(gfs, path, false);
}

int ghostfs_rmdir(struct ghostfs *gfs, const char *path)
{
	return remove_entry(gfs, path, true);
}

static int last_cluster(struct ghostfs *gfs, int first, struct cluster **pcluster)
{
	int ret;

	for (;;) {
		struct cluster *c;

		ret = cluster_get(gfs, first, &c);
		if (ret < 0)
			return ret;

		if (!c->hdr.next) {
			if (pcluster)
				*pcluster = c;
			break;
		}
		first = c->hdr.next;
	}

	return first;
}

static int size_to_clusters(int size)
{
	return size / CLUSTER_DATA + (size % CLUSTER_DATA ? 1 : 0);
}

static int do_truncate(struct ghostfs *gfs, struct dir_iter *it, off_t new_size)
{
	int old_nr, nr;
	int ret;

	if (new_size < 0)
		return -EINVAL;
	if (new_size > FILESIZE_MAX)
		return -EFBIG;

	if (dir_entry_is_directory(it->entry))
		return -EISDIR;

	old_nr = size_to_clusters(it->entry->size);
	nr = size_to_clusters(new_size);

	if (nr > old_nr) { // increase cluster count
		struct cluster *last = NULL;

		if (it->entry->cluster) {
			ret = last_cluster(gfs, it->entry->cluster, &last);
			if (ret < 0)
				return ret;
		}

		ret = alloc_clusters(gfs, nr - old_nr, NULL, false);
		if (ret < 0)
			return ret;

		if (last) {
			last->hdr.next = ret;
			cluster_set_dirty(last, true);
		} else {
			it->entry->cluster = ret;
		}
	} else if (nr < old_nr) { // decrease cluster count
		struct cluster *c;
		int i;
		int next = it->entry->cluster;

		for (i = 0; i < nr; i++) {
			ret = cluster_get(gfs, next, &c);
			if (ret < 0)
				return ret;
			next = c->hdr.next;
		}

		if (nr) {
			cluster_set_dirty(c, true);
			c->hdr.next = 0;
		} else {
			it->entry->cluster = 0;
		}

		ret = cluster_get(gfs, next, &c);
		if (ret < 0)
			return ret;

		free_clusters(gfs, c);
	}

	dir_entry_set_size(it->entry, new_size, false);
	cluster_set_dirty(it->cluster, true);

	return 0;
}

int ghostfs_truncate(struct ghostfs *gfs, const char *path, off_t new_size)
{
	struct dir_iter it;
	int ret;

	ret = dir_iter_lookup(gfs, &it, path, false);
	if (ret < 0)
		return ret;

	return do_truncate(gfs, &it, new_size);
}

int ghostfs_open(struct ghostfs *gfs, const char *filename, struct ghostfs_entry **pentry)
{
	struct dir_iter it;
	int ret;

	ret = dir_iter_lookup(gfs, &it, filename, false);
	if (ret < 0)
		return ret;

	if (dir_entry_is_directory(it.entry))
		return -EISDIR;

	if (pentry) {
		*pentry = malloc(sizeof(**pentry));
		if (!*pentry)
			return -ENOMEM;
		(*pentry)->it = it;
	}

	return 0;
}

int ghostfs_release(struct ghostfs *gfs, struct ghostfs_entry *entry)
{
	free(entry);
	return 0;
}

int ghostfs_write(struct ghostfs *gfs, struct ghostfs_entry *gentry, const char *buf, size_t size, off_t offset)
{
	struct dir_entry *entry = gentry->it.entry;
	struct cluster *c;
	int next, nr;
	int ret;
	int written = 0;

	if (offset < 0)
		return -EINVAL;

	if (entry->size < offset + size) {
		ret = do_truncate(gfs, &gentry->it, offset + size);
		if (ret < 0)
			return ret;
	}

	nr = size_to_clusters(entry->size);
	next = entry->cluster;

	while (nr && next) {
		ret = cluster_get(gfs, next, &c);
		if (ret < 0)
			return ret;
		next = c->hdr.next;
		nr--;
	}
	if (nr) {
		warnx("fs: cluster missing, bad filesystem");
		return -EIO;
	}

	// adjust offset to first cluster
	offset %= CLUSTER_DATA;

	while (size) {
		int w = (size < CLUSTER_DATA) ? size : CLUSTER_DATA;
		if (offset + w > CLUSTER_DATA)
			w -= (offset + w) - CLUSTER_DATA;

		memcpy(c->data + offset, buf, w);
		cluster_set_dirty(c, true);

		size -= w;
		buf += w;
		written += w;
		offset = 0;

		if (!c->hdr.next)
			break;

		ret = cluster_get(gfs, c->hdr.next, &c);
		if (ret < 0)
			return ret;
	}
	if (size) {
		warnx("fs: cluster missing, bad filesystem");
		return -EIO;
	}

	return written;
}

int ghostfs_read(struct ghostfs *gfs, struct ghostfs_entry *gentry, char *buf, size_t size, off_t offset)
{
	struct dir_entry *entry = gentry->it.entry;
	struct cluster *c;
	int next, nr;
	int ret;
	int read = 0;

	if (offset < 0)
		return -EINVAL;
	if (entry->size < offset + size)
		return -EINVAL;

	nr = size_to_clusters(entry->size);
	next = entry->cluster;

	while (nr && next) {
		ret = cluster_get(gfs, next, &c);
		if (ret < 0)
			return ret;
		next = c->hdr.next;
		nr--;
	}
	if (nr) {
		warnx("fs: cluster missing, bad filesystem");
		return -EIO;
	}

	// adjust offset to first cluster
	offset %= CLUSTER_DATA;

	while (size) {
		int r = (size < CLUSTER_DATA) ? size : CLUSTER_DATA;
		if (offset + r > CLUSTER_DATA)
			r -= (offset + r) - CLUSTER_DATA;

		memcpy(buf, c->data + offset, r);

		size -= r;
		buf += r;
		read += r;
		offset = 0;

		if (!c->hdr.next)
			break;

		ret = cluster_get(gfs, c->hdr.next, &c);
		if (ret < 0)
			return ret;
	}
	if (size) {
		warnx("fs: cluster missing, bad filesystem");
		return -EIO;
	}

	return read;
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
				printf(" {%d}\n", it.entry->size);
			}
		}
	} while (dir_iter_next_used(&it) == 0);
}

void ghostfs_debug(struct ghostfs *gfs)
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

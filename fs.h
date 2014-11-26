#ifndef GHOST_FS_H
#define GHOST_FS_H

struct ghostfs;

enum {
	GHOSTFS_UNFORMATTED = 1,
};

int ghostfs_open(struct ghostfs **pgfs, const char *filename);
int ghostfs_close(struct ghostfs *gfs);

int ghostfs_format(struct ghostfs *gfs);
int ghostfs_status(const struct ghostfs *gfs);
int ghostfs_cluster_count(const struct ghostfs *gfs);

#endif // GHOST_FS_H

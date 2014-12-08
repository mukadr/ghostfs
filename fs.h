#ifndef GHOST_FS_H
#define GHOST_FS_H

struct ghostfs;

enum {
	GHOSTFS_OK = 0,
	GHOSTFS_UNFORMATTED,
};

int ghostfs_mount(struct ghostfs **pgfs, const char *filename);
int ghostfs_umount(struct ghostfs *gfs);

int ghostfs_create(struct ghostfs *gfs, const char *path);
int ghostfs_mkdir(struct ghostfs *gfs, const char *path);

int ghostfs_format(struct ghostfs *gfs);
int ghostfs_status(const struct ghostfs *gfs);
int ghostfs_cluster_count(const struct ghostfs *gfs);

#endif // GHOST_FS_H

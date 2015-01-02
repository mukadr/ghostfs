#ifndef GHOST_FS_H
#define GHOST_FS_H

struct ghostfs;

int ghostfs_mount(struct ghostfs **pgfs, const char *filename);
int ghostfs_umount(struct ghostfs *gfs);

int ghostfs_create(struct ghostfs *gfs, const char *path);
int ghostfs_unlink(struct ghostfs *gfs, const char *path);
int ghostfs_mkdir(struct ghostfs *gfs, const char *path);
int ghostfs_rmdir(struct ghostfs *gfs, const char *path);

int ghostfs_format(const char *filename);
int ghostfs_status(const struct ghostfs *gfs);
int ghostfs_cluster_count(const struct ghostfs *gfs);
void ghostfs_debug(struct ghostfs *gfs);

#endif // GHOST_FS_H

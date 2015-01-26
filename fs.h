#ifndef GHOST_FS_H
#define GHOST_FS_H

#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

struct ghostfs;
struct ghostfs_entry;

int ghostfs_mount(struct ghostfs **pgfs, const char *filename);
int ghostfs_umount(struct ghostfs *gfs);

int ghostfs_create(struct ghostfs *gfs, const char *path);
int ghostfs_unlink(struct ghostfs *gfs, const char *path);
int ghostfs_mkdir(struct ghostfs *gfs, const char *path);
int ghostfs_rmdir(struct ghostfs *gfs, const char *path);
int ghostfs_truncate(struct ghostfs *gfs, const char *path, off_t new_size);
int ghostfs_open(struct ghostfs *gfs, const char *filename, struct ghostfs_entry **pentry);
void ghostfs_release(struct ghostfs_entry *entry);
int ghostfs_write(struct ghostfs *gfs, struct ghostfs_entry *gentry, const char *buf, size_t size, off_t offset);
int ghostfs_read(struct ghostfs *gfs, struct ghostfs_entry *gentry, char *buf, size_t size, off_t offset);
int ghostfs_opendir(struct ghostfs *gfs, const char *path, struct ghostfs_entry **pentry);
int ghostfs_next_entry(struct ghostfs *gfs, struct ghostfs_entry *entry);
void ghostfs_closedir(struct ghostfs_entry *entry);
const char *ghostfs_entry_name(const struct ghostfs_entry *entry);
int ghostfs_getattr(struct ghostfs *gfs, const char *filename, struct stat *stat);

int ghostfs_format(const char *filename);
int ghostfs_status(const struct ghostfs *gfs);
int ghostfs_cluster_count(const struct ghostfs *gfs);
void ghostfs_debug(struct ghostfs *gfs);

#endif // GHOST_FS_H

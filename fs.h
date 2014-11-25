#ifndef GHOST_FS_H
#define GHOST_FS_H

struct ghostfs;

int ghostfs_open(struct ghostfs **pgfs, const char *filename);
int ghostfs_close(struct ghostfs *gfs);

#endif // GHOST_FS_H

#define FUSE_USE_VERSION 26
#include <fuse.h>
#include <stdio.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fs.h"

static int gfs_fuse_unlink(const char *path)
{
	return -ENOSYS;
}

static int gfs_fuse_mkdir(const char *path, mode_t mode)
{
	return -ENOSYS;
}

static int gfs_fuse_rmdir(const char *path)
{
	return -ENOSYS;
}

static int gfs_fuse_truncate(const char *path, off_t newsize)
{
	return -ENOSYS;
}

static int gfs_fuse_open(const char *path, struct fuse_file_info *info)
{
	return -ENOSYS;
}

static int gfs_fuse_release(const char *path, struct fuse_file_info *info)
{
	return -ENOSYS;
}

static int gfs_fuse_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *info)
{
	return -ENOSYS;
}

static int gfs_fuse_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *info)
{
	return -ENOSYS;
}

static int gfs_fuse_opendir(const char *path, struct fuse_file_info *info)
{
	return -ENOSYS;
}

static int gfs_fuse_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *info)
{
	return -ENOSYS;
}

static int gfs_fuse_releasedir(const char *path, struct fuse_file_info *info)
{
	return -ENOSYS;
}

static int gfs_fuse_getattr(const char *path, struct stat *stat)
{
	return -ENOSYS;
}

static int gfs_fuse_rename(const char *path, const char *newpath)
{
	return -ENOSYS;
}

static int gfs_fuse_statfs(const char *path, struct statvfs *stat)
{
	return -ENOSYS;
}

void *init(struct fuse_conn_info *conn)
{
	return NULL;
}

void destroy(void *user)
{
}

struct fuse_operations operations = {
	.init = init,
	.destroy = destroy,

	.unlink = gfs_fuse_unlink,
	.mkdir = gfs_fuse_mkdir,
	.rmdir = gfs_fuse_rmdir,
	.truncate = gfs_fuse_truncate,
	.open = gfs_fuse_open,
	.release = gfs_fuse_release,
	.write = gfs_fuse_write,
	.read = gfs_fuse_read,
	.opendir = gfs_fuse_opendir,
	.readdir = gfs_fuse_readdir,
	.releasedir = gfs_fuse_releasedir,
	.getattr = gfs_fuse_getattr,
	.rename = gfs_fuse_rename,
	.statfs = gfs_fuse_statfs,
};

int main(int argc, char *argv[])
{
	return fuse_main(argc, argv, &operations, NULL);
}

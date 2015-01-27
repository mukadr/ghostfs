#define FUSE_USE_VERSION 26
#include <fuse.h>

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fs.h"

static struct ghostfs *get_gfs(void)
{
	return fuse_get_context()->private_data;
}

static int gfs_fuse_unlink(const char *path)
{
	return ghostfs_unlink(get_gfs(), path);
}

static int gfs_fuse_mkdir(const char *path, mode_t mode)
{
	return ghostfs_mkdir(get_gfs(), path);
}

static int gfs_fuse_rmdir(const char *path)
{
	return ghostfs_rmdir(get_gfs(), path);
}

static int gfs_fuse_truncate(const char *path, off_t newsize)
{
	return ghostfs_truncate(get_gfs(), path, newsize);
}

static int gfs_fuse_create(const char *path, mode_t mode, struct fuse_file_info *info)
{
	struct ghostfs *gfs = get_gfs();
	struct ghostfs_entry *entry;
	int ret;

	ret = ghostfs_create(gfs, path);
	if (ret < 0)
		return ret;

	ret = ghostfs_open(gfs, path, &entry);
	if (ret < 0) {
		ghostfs_unlink(gfs, path);
		return ret;
	}

	info->fh = (intptr_t)entry;
	return 0;
}

static int gfs_fuse_open(const char *path, struct fuse_file_info *info)
{
	struct ghostfs *gfs = get_gfs();
	struct ghostfs_entry *entry;
	int ret;

	ret = ghostfs_open(gfs, path, &entry);
	if (ret < 0)
		return ret;

	info->fh = (intptr_t)entry;
	return 0;
}

static int gfs_fuse_release(const char *path, struct fuse_file_info *info)
{
	ghostfs_release((struct ghostfs_entry *)info->fh);
	return 0;
}

static int gfs_fuse_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *info)
{
	return ghostfs_write(get_gfs(), (struct ghostfs_entry *)info->fh, buf, size, offset);
}

static int gfs_fuse_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *info)
{
	return ghostfs_read(get_gfs(), (struct ghostfs_entry *)info->fh, buf, size, offset);
}

static int gfs_fuse_opendir(const char *path, struct fuse_file_info *info)
{
	struct ghostfs *gfs = get_gfs();
	struct ghostfs_entry *dp;
	int ret;

	ret = ghostfs_opendir(gfs, path, &dp);
	if (ret < 0)
		return ret;

	info->fh = (intptr_t)dp;
	return 0;
}

static int gfs_fuse_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *info)
{
	struct ghostfs *gfs = get_gfs();
	struct ghostfs_entry *dp = (struct ghostfs_entry *)info->fh;
	int ret;

	while ((ret = ghostfs_next_entry(gfs, dp)) == 0) {
		if (filler(buf, ghostfs_entry_name(dp), NULL, 0) != 0)
			return -ENOMEM;
	}

	if (ret != -ENOENT)
		return ret;

	return 0;
}

static int gfs_fuse_releasedir(const char *path, struct fuse_file_info *info)
{
	ghostfs_closedir((struct ghostfs_entry *)info->fh);
	return 0;
}

static int gfs_fuse_getattr(const char *path, struct stat *stat)
{
	return ghostfs_getattr(get_gfs(), path, stat);
}

static int gfs_fuse_rename(const char *path, const char *newpath)
{
	return ghostfs_rename(get_gfs(), path, newpath);
}

static int gfs_fuse_statfs(const char *path, struct statvfs *stat)
{
	return ghostfs_statvfs(get_gfs(), stat);
}

void *init(struct fuse_conn_info *conn)
{
	return get_gfs();
}

void destroy(void *user)
{
	struct ghostfs *gfs = user;
	int ret;

	ret = ghostfs_umount(gfs);
	if (ret < 0)
		fprintf(stderr, "failed to write filesystem: %s\n", strerror(-ret));
}

struct fuse_operations operations = {
	.init = init,
	.destroy = destroy,

	.unlink = gfs_fuse_unlink,
	.mkdir = gfs_fuse_mkdir,
	.rmdir = gfs_fuse_rmdir,
	.truncate = gfs_fuse_truncate,
	.create = gfs_fuse_create,
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
	char *fuse_argv[4];
	struct ghostfs *gfs;
	int ret;
	bool debug;

	if (argc < 3) {
		fprintf(stderr, "usage: ghost-fuse file mount_point [d]\n");
		return 1;
	}

	debug = (argc > 3) && !strcmp(argv[3], "d");

	ret = ghostfs_mount(&gfs, argv[1]);
	if (ret < 0) {
		fprintf(stderr, "failed to mount: %s\n", strerror(-ret));
		return 1;
	}

	fuse_argv[0] = argv[0];
	fuse_argv[1] = argv[2];
	// disable multithreading
	fuse_argv[2] = "-s";
	if (debug)
		fuse_argv[3] = "-d";

	return fuse_main(debug ? 4 : 3, fuse_argv, &operations, gfs);
}

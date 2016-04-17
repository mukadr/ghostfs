#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FUSE_USE_VERSION 26
#include <fuse.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fs.h"
#include "passwd.h"
#include "util.h"

struct gfs_context {
	struct sampler *sampler;
	struct stegger *stegger;
	struct ghostfs *gfs;
};

static struct ghostfs *get_gfs(void)
{
	return ((struct gfs_context *)fuse_get_context()->private_data)->gfs;
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
	return ghostfs_open(get_gfs(), path, (struct ghostfs_entry **)&info->fh);
}

static int gfs_fuse_release(const char *path, struct fuse_file_info *info)
{
	ghostfs_release((struct ghostfs_entry *)info->fh);
	return 0;
}

static int gfs_fuse_write(const char *path, const char *buf, size_t size, off_t offset,
			  struct fuse_file_info *info)
{
	return ghostfs_write(get_gfs(), (struct ghostfs_entry *)info->fh, buf, size, offset);
}

static int gfs_fuse_read(const char *path, char *buf, size_t size, off_t offset,
			 struct fuse_file_info *info)
{
	return ghostfs_read(get_gfs(), (struct ghostfs_entry *)info->fh, buf, size, offset);
}

static int gfs_fuse_opendir(const char *path, struct fuse_file_info *info)
{
	return ghostfs_opendir(get_gfs(), path, (struct ghostfs_entry **)&info->fh);
}

static int gfs_fuse_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			    off_t offset, struct fuse_file_info *info)
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

static int gfs_fuse_chmod(const char *path, mode_t mode)
{
	return 0;
}

static int gfs_fuse_chown(const char *path, uid_t uid, gid_t gid)
{
	return 0;
}

void destroy(void *user)
{
	struct gfs_context *ctx = user;
	int ret;

	ret = ghostfs_umount(ctx->gfs);
	if (ret < 0)
		fprintf(stderr, "failed to write filesystem: %s\n", strerror(-ret));

	ret = stegger_close(ctx->stegger);
	if (ret < 0)
		fprintf(stderr, "failed to write filesystem: %s\n", strerror(-ret));

	ret = sampler_close(ctx->sampler);
	if (ret < 0)
		fprintf(stderr, "failed to write filesystem: %s\n", strerror(-ret));
}

struct fuse_operations operations = {
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
	.chmod = gfs_fuse_chmod,
	.chown = gfs_fuse_chown
};

int main(int argc, char *argv[])
{
	char *fuse_argv[4];
	int ret;
	bool debug;
	struct gfs_context ctx;
	const char *env;

	if (argc < 3) {
		fprintf(stderr, "usage: ghost-fuse file mount_point <password>\n");
		return 1;
	}

	ret = open_sampler_by_extension(&ctx.sampler, argv[1]);
	if (ret < 0) {
		fprintf(stderr, "invalid format");
		return 1;
	}

	if (argc == 4) {
		ret = passwd_open(&ctx.stegger, ctx.sampler, argv[3]);
		if (ret < 0) {
			fprintf(stderr, "failed to mount: %s\n", strerror(-ret));
			return 1;
		}

		ret = ghostfs_mount(&ctx.gfs, ctx.stegger);
		if (ret < 0) {
			fprintf(stderr, "failed to mount: %s\n", strerror(-ret));
			return 1;
		}
	} else {
		ret = try_mount_lsb(&ctx.gfs, &ctx.stegger, ctx.sampler);
		if (ret < 0) {
			fprintf(stderr, "failed to mount: %s\n", strerror(-ret));
			return 1;
		}
	}

	fuse_argv[0] = argv[0];
	fuse_argv[1] = argv[2];
	// disable multithreading
	fuse_argv[2] = "-s";

	env = getenv("GHOSTFS_DEBUG");
	debug = env && atoi(env);
	if (debug)
		fuse_argv[3] = "-d";

	return fuse_main(debug ? 4 : 3, fuse_argv, &operations, &ctx);
}

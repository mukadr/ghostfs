#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs.h"
#include "lsb.h"
#include "passwd.h"
#include "util.h"

int main(int argc, char *argv[])
{
	struct sampler *sampler = NULL;
	struct stegger *stegger = NULL;
	struct ghostfs *gfs = NULL;
	int ret;

	if (argc < 2) {
		printf("usage: ghost <file> <cmd> {args}\n");
		return 1;
	}

	ret = open_sampler_by_extension(&sampler, argv[1]);
	if (ret < 0)
		goto umount;

	if (argc == 4 && strcmp(argv[2], "f") == 0) {
		ret = lsb_open(&stegger, sampler, atoi(argv[3]));
		if (ret < 0)
			goto umount;

		ret = ghostfs_format(stegger);

		goto umount;
	}

	if (argc == 4 && strcmp(argv[2], "fp") == 0) {
		ret = passwd_open(&stegger, sampler, argv[3]);
		if (ret < 0)
			goto umount;

		ret = ghostfs_format(stegger);

		goto umount;
	}

	ret = try_mount_lsb(&gfs, &stegger, sampler);
	if (ret < 0)
		goto umount;

	printf("cluster count = %d\n", ghostfs_cluster_count(gfs));

	if (argc < 3)
		return 0;

	switch (argv[2][0]) {
	case 'c':
		if (argc != 4) {
			printf("create: missing filename\n");
			return 1;
		}
		ret = ghostfs_create(gfs, argv[3]);
		if (ret < 0)
			goto umount;
		break;
	case 'd':
		if (argc != 4) {
			printf("mkdir: missing filename\n");
			return 1;
		}
		ret = ghostfs_mkdir(gfs, argv[3]);
		if (ret < 0)
			goto umount;
		break;
	case 'r':
		if (argc != 4) {
			printf("unlink: missing filename\n");
			return 1;
		}
		ret = ghostfs_unlink(gfs, argv[3]);
		if (ret < 0)
			goto umount;
		break;
	case 'R':
		if (argc != 4) {
			printf("rmdir: missing filename\n");
			return 1;
		}
		ret = ghostfs_rmdir(gfs, argv[3]);
		if (ret < 0)
			goto umount;
		break;
	case 't':
		if (argc != 5) {
			printf("truncate: missing filename and size\n");
			return 1;
		}
		ret = ghostfs_truncate(gfs, argv[3], atol(argv[4]));
		if (ret < 0)
			goto umount;
		break;
	case '1': {
		struct ghostfs_entry *e;

		if (argc != 5) {
			printf("write test: missing filename and offset\n");
			return 1;
		}

		ret = ghostfs_open(gfs, argv[3], &e);
		if (ret < 0)
			goto umount;

		ret = ghostfs_write(gfs, e, "Hello World!", 12, atol(argv[4]));
		if (ret < 0)
			goto umount;

		ghostfs_release(e);
		break;
	}
	case '2': {
		struct ghostfs_entry *e;
		char buf[12];

		if (argc != 5) {
			printf("read test: missing filename and offset\n");
			return 1;
		}

		ret = ghostfs_open(gfs, argv[3], &e);
		if (ret < 0)
			goto umount;

		ret = ghostfs_read(gfs, e, buf, sizeof(buf), atol(argv[4]));
		if (ret < 0)
			goto umount;

		fwrite(buf, sizeof(buf), 1, stdout);
		printf("\n");

		ghostfs_release(e);
		break;
	}
	case 'l': {
		struct ghostfs_entry *e;

		if (argc != 4) {
			printf("ls: missing filename\n");
			return 1;
		}

		ret = ghostfs_opendir(gfs, argv[3], &e);
		if (ret < 0)
			goto umount;

		while ((ret = ghostfs_next_entry(gfs, e)) == 0)
			printf("'%s'\n", ghostfs_entry_name(e));

		if (ret != -ENOENT)
			goto umount;

		ghostfs_closedir(e);
		break;
	}
	case '?':
		ret = ghostfs_debug(gfs);
		if (ret < 0)
			goto umount;
		break;
	case 'm':
		if (argc != 5) {
			printf("mv: missing path newpath\n");
			return 1;
		}

		ret = ghostfs_rename(gfs, argv[3], argv[4]);
		if (ret < 0)
			goto umount;

		break;
	}

umount:
	if (ret < 0)
		fprintf(stderr, "error: %s\n", strerror(-ret));

	if (gfs) {
		ret = ghostfs_umount(gfs);
		if (ret < 0)
			fprintf(stderr, "error: %s\n", strerror(-ret));
	}

	if (stegger) {
		ret = stegger_close(stegger);
		if (ret < 0)
			fprintf(stderr, "error: %s\n", strerror(-ret));
	}

	if (sampler) {
		ret = sampler_close(sampler);
		if (ret < 0)
			fprintf(stderr, "error: %s\n", strerror(-ret));
	}

	return ret < 0;
}

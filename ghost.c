#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "fs.h"

int main(int argc, char *argv[])
{
	struct ghostfs *gfs = NULL;
	int ret;

	if (argc < 2) {
		printf("usage: ghost <file> <cmd> {args}\n");
		return 1;
	}

	if (argc == 3 && argv[2][0] == 'f') {
		ret = ghostfs_format(argv[1]);
		if (ret < 0)
			goto failed;
		return 0;
	}

	ret = ghostfs_mount(&gfs, argv[1]);
	if (ret < 0)
		goto failed;

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
			goto failed;
		break;
	case 'd':
		if (argc != 4) {
			printf("mkdir: missing filename\n");
			return 1;
		}
		ret = ghostfs_mkdir(gfs, argv[3]);
		if (ret < 0)
			goto failed;
		break;
	case 'r':
		if (argc != 4) {
			printf("unlink: missing filename\n");
			return 1;
		}
		ret = ghostfs_unlink(gfs, argv[3]);
		if (ret < 0)
			goto failed;
		break;
	case 'R':
		if (argc != 4) {
			printf("rmdir: missing filename\n");
			return 1;
		}
		ret = ghostfs_rmdir(gfs, argv[3]);
		if (ret < 0)
			goto failed;
		break;
	case 't':
		if (argc != 5) {
			printf("truncate: missing filename and size\n");
			return 1;
		}
		ret = ghostfs_truncate(gfs, argv[3], atol(argv[4]));
		if (ret < 0)
			goto failed;
		break;
	case '1': {
		struct ghostfs_entry *e;

		if (argc != 5) {
			printf("write test: missing filename and offset\n");
			return 1;
		}

		ret = ghostfs_open(gfs, argv[3], &e);
		if (ret < 0)
			goto failed;

		ret = ghostfs_write(gfs, e, "Hello World!", 12, atol(argv[4]));
		if (ret < 0)
			goto failed;

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
			goto failed;

		ret = ghostfs_read(gfs, e, buf, sizeof(buf), atol(argv[4]));
		if (ret < 0)
			goto failed;

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
			goto failed;

		while ((ret = ghostfs_next_entry(gfs, e)) == 0)
			printf("'%s'\n", ghostfs_entry_name(e));

		if (ret != -ENOENT)
			goto failed;

		ghostfs_closedir(e);
		break;
	}
	case '?':
		ret = ghostfs_debug(gfs);
		if (ret < 0)
			goto failed;
		break;
	case 'm':
		if (argc != 5) {
			printf("mv: missing path newpath\n");
			return 1;
		}

		ret = ghostfs_rename(gfs, argv[3], argv[4]);
		if (ret < 0)
			goto failed;

		break;
	}

	ret = ghostfs_umount(gfs);
	if (ret < 0)
		goto failed;

	return 0;
failed:
	fprintf(stderr, "error: %s\n", strerror(-ret));
	if (gfs)
		ghostfs_umount(gfs);
	return 1;
}

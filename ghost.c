#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "fs.h"

int main(int argc, char *argv[])
{
	struct ghostfs *gfs;
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
	case '?':
		ghostfs_debug(gfs);
		break;
	}

	ret = ghostfs_umount(gfs);
	if (ret < 0)
		goto failed;

	return 0;
failed:
	fprintf(stderr, "error: %s\n", strerror(-ret));
	return 1;
}

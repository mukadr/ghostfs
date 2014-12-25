#include <string.h>
#include <stdio.h>

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

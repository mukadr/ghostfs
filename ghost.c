#include <stdio.h>

#include "fs.h"

int main(int argc, char *argv[])
{
	struct ghostfs *gfs;

	if (argc < 3) {
		printf("usage: ghost <file> <cmd> {args}\n");
		return 1;
	}

	if (ghostfs_mount(&gfs, argv[1]) < 0)
		return 1;

	switch (argv[2][0]) {
	case 'p':
		switch (ghostfs_status(gfs)) {
		case GHOSTFS_UNFORMATTED:
			printf("unformatted media\n");
			break;
		case GHOSTFS_OK:
			printf("formatted media\n");
			printf("cluster count = %d\n", ghostfs_cluster_count(gfs));
			break;
		}
		break;
	case 'f':
		if (ghostfs_format(gfs) < 0)
			return 1;
		break;
	case 'c':
		if (argc != 4) {
			printf("create: missing filename\n");
			return 1;
		}
		if (ghostfs_create(gfs, argv[3]) < 0)
			return 1;
		break;
	}

	if (ghostfs_umount(gfs) < 0)
		return 1;

	return 0;
}

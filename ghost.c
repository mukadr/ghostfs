#include <stdio.h>

#include "fs.h"

int main(int argc, char *argv[])
{
	struct ghostfs *gfs;

	if (argc < 3) {
		printf("usage: ghost <cmd> <file>\n");
		return 1;
	}

	if (ghostfs_open(&gfs, argv[2]) < 0)
		return 1;

	switch (argv[1][0]) {
	case 'p':
		switch (ghostfs_status(gfs)) {
		case GHOSTFS_UNFORMATTED:
			printf("unformatted media\n");
			break;
		case 0:
			printf("formatted media\n");
			printf("cluster count = %d\n", ghostfs_cluster_count(gfs));
			break;
		}
		break;
	case 'f':
		if (ghostfs_format(gfs) < 0)
			return 1;
		break;
	}

	if (ghostfs_close(gfs) < 0)
		return 1;

	return 0;
}

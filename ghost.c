#include <stdio.h>

#include "fs.h"

int main(int argc, char *argv[])
{
	struct ghostfs *gfs;

	if (argc < 2) {
		printf("usage: ghost <file>\n");
		return 1;
	}
	if (ghostfs_open(&gfs, argv[1]) < 0)
		return 1;
	ghostfs_close(gfs);
	return 0;
}

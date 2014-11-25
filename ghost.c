#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "steg.h"

struct steg_ops dummy_ops = {
	NULL,
};

int main(int argc, char *argv[])
{
	struct steg steg;

	if (argc < 2) {
		printf("usage: ghost <file>\n");
		return 1;
	}
	if (steg_open(&steg, argv[1], &dummy_ops) < 0)
		return 1;
	steg_close(&steg);
	return 0;
}

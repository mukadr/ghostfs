#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "steg_wav.h"

struct steg_ops dummy_ops = {
	NULL,
};

int main(int argc, char *argv[])
{
	struct steg *steg;
	unsigned short temp;

	if (argc < 2) {
		printf("usage: ghost <file>\n");
		return 1;
	}
	if (wav_open(&steg, argv[1]) < 0)
		return 1;
	if (steg_read(steg, &temp, 2, 0, 1) < 0)
		return 1;
	printf("%d\n", temp);
	temp++;
	steg_write(steg, &temp, 2, 0, 1);
	steg_close(steg);
	return 0;
}

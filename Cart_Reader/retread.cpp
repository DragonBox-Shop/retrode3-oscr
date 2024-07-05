/* new driver */

#include <stdio.h>
#include <stdlib.h>

void usage(void)
{
	printf("usage: retread snes|megadrive|nes command parameters...\n");
	exit(1);
}

int main(int argc, char *argv[])
{
	// decode argv[1] for "snes", "megadrive", "nes"
	// prepare user choices
	// call appropriate subfunctions from the .ino modules
	usage();
	exit(0);
}
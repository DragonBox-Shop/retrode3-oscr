// Library to access retrode3 slots and devices
// (C) by H. N. Schaller (hns@goldelico.com) - licenced under GPL V3

#include "retrode-lib.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

// call these from within the patched MD.ino

#if 1
#define DEBUG(FORMAT, ...) fprintf(stderr, "%s %d: " FORMAT "\n", __PRETTY_FUNCTION__, __LINE__, ##__VA_ARGS__)
#else
#define DEBUG(FORMAT, ...)
#endif

/* wrappers for direct access */
static int fd_md = -1;

int open_md(void)
{
	DEBUG("%s %d:\n");

	if (fd_md < 0) {
		fd_md = open("/dev/slot-md", O_RDWR);
		if (fd_md < 0) {
			perror("no cart in MD slot");
			return -1;
		}
	}
	return fd_md;
}

int close_md(void)
{
	if (fd_md >= 0) {
		if (close(fd_md) < 0) {
			perror("can't close MD slot");
			return -1;	/* unexpected error */
		}
	}
	fd_md = -1;
	return 0;
}

int read_md(unsigned long addr, void *buf, int size, int mode)
{
	int ret;
	DEBUG("%s %d: addr=%06x data=%04x mode=%d\n", addr, size, mode);
	if (open_md() < 0)
		return -1;
	addr = (mode << 24) + ((addr) << 1);
	DEBUG("%s %d: addr=%06x\n", addr);
	if (lseek(fd_md, addr, SEEK_SET) < 0) {
		perror("seek error on MD slot");
		return -1;
	}
	ret = read(fd_md, buf, size);
	if (ret < 0)
		perror("read error on MD slot");
	return ret;
}

int write_md(unsigned long addr, void *buf, int size, int mode)
{
	int ret;
	DEBUG("%s %d: addr=%06x data=%04x mode=%d\n", addr, size, mode);
	if (open_md() < 0)
		return -1;
	addr = (mode << 24) + ((addr) << 1);
	DEBUG("%s %d: addr=%06x\n", addr);
	if (lseek(fd_md, addr, SEEK_SET) < 0) {
		perror("seek error on MD slot");
		return -1;
	}
	ret = write(fd_md, buf, size);
	if (ret < 0)
		perror("write error on MD slot");
	return ret;
}

int set_voltage_md(int mV)
{ /* 3300 or 5000 */
	// control /sys node
}

// more wrappers for SNES, NES
// also for reading/writing RAM in different modes

// the goal is to hide all kernel interfaces from user space
// and to allow to write plugins for cards that are plugged in through adapters

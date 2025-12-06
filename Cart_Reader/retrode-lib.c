// Library to access retrode3 slots and devices
// (C) by H. N. Schaller (hns@goldelico.com) - licenced under GPL V3

#include "retrode-lib.h"
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

// call these from within the patched MD.ino

/* wrappers for direct access */
static int fd_md = -1;

int open_md(void)
{
	if (fd_md >= 0)
		return -1;	// already open
	fd_md = open("/dev/slot-md", O_RDWR);
	return fd_md >= 0;
}

int close_md(void)
{
	if (fd_md < 0)
		return -1;
	close(fd_md);
	fd_md = -1;
	return 0;
}

int read_md(uint32_t addr, uint16_t size, uint16_t *buf)
{
	if (fd_md < 0 && open_md() < 0)
		return -1;
	if (lseek(fd_md, addr, 0) < 0)
		return -1;
	return read(fd_md, buf, size);
}

int write_md(uint32_t addr, uint16_t size, uint16_t *buf)
{

}

int time_read_md(uint32_t addr, uint16_t size, uint16_t *buf)
{

}

int time_md(bool on_off)	/* activate/deactivate time */
{

}

int reset_md(bool on_off)	/* activate/deactivate reset */
{

}

int select_md(bool on_off)	/* activate/deactivate cart enable */
{

}


// more wrappers for SNES, NES
// also for reading/writing RAM in different modes

// the goal is to hide all kernel interfaces from user space
// and to allow to write plugins for cards that are plugged in through adapters

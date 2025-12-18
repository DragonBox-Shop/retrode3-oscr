// Library to access retrode3 slots and devices
// (C) by H. N. Schaller (hns@goldelico.com) - licenced under GPL V3

#include "retrode-lib.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#if 1
#define DEBUG(FORMAT, ...) fprintf(stderr, "%s %d: " FORMAT "\n", __PRETTY_FUNCTION__, __LINE__, ##__VA_ARGS__)
#else
#define DEBUG(FORMAT, ...)
#endif

/* wrappers for direct access */
static int md_fd = -1;
static int nes_fd = -1;
static int snes_fd = -1;

int md_open(void)
{
	DEBUG("");
	if (md_fd < 0) {
		md_fd = open("/dev/slot-md", O_RDWR);
		if (md_fd < 0) {
			perror("no cart in MD slot");
			return -1;
		}
	}
	return md_fd;
}

int md_close(void)
{
	DEBUG("");
	if (md_fd >= 0) {
		if (close(md_fd) < 0) {
			perror("can't close MD slot");
			return -1;	/* unexpected error */
		}
	}
	md_fd = -1;
	return 0;
}

int md_read(uint32_t addr, void *buf, uint32_t size, int mode)
{
	int ret;
	DEBUG("addr=%06x data=%04x mode=%d", addr, size, mode);
	if (md_open() < 0)
		return -1;
	addr = (mode << 24) + addr;
	DEBUG("addr=%06x", addr);
	if (lseek(md_fd, addr, SEEK_SET) < 0) {
		perror("seek error on MD slot");
		return -1;
	}
	ret = read(md_fd, buf, size);
	if (ret < 0)
		perror("read error on MD slot");
	return ret;
}

int md_write(uint32_t addr, void *buf, uint32_t size, int mode)
{
	int ret;
	DEBUG("addr=%06x data=%04x mode=%d", addr, size, mode);
	if (md_open() < 0)
		return -1;
	addr = (mode << 24) + addr;
	DEBUG("addr=%06x", addr);
	if (lseek(md_fd, addr, SEEK_SET) < 0) {
		perror("seek error on MD slot");
		return -1;
	}
	ret = write(md_fd, buf, size);
	if (ret < 0)
		perror("write error on MD slot");
	return ret;
}

int md_set_voltage(int mV)
{ /* 3300 or 5000 */
	DEBUG("");
	// control /sys node
}

int nes_open(void)
{
	DEBUG("");
	if (nes_fd < 0) {
		nes_fd = open("/dev/slot-nes", O_RDWR);
		if (nes_fd < 0) {
			perror("no cart in NES slot");
			return -1;
		}
	}
	return nes_fd;
}

int nes_close(void)
{
	DEBUG("");
	if (nes_fd >= 0) {
		if (close(nes_fd) < 0) {
			perror("can't close NES slot");
			return -1;	/* unexpected error */
		}
	}
	nes_fd = -1;
	return 0;
}

int nes_read(uint16_t addr, void *buf, uint16_t size, int mode)
{
	int ret;
	uint32_t a = (mode << 24) + addr;
	DEBUG("addr=%04x data=%04x mode=%d", addr, size, mode);
	if (nes_open() < 0)
		return -1;
	DEBUG("addr=%08x", a);
	if (lseek(nes_fd, a, SEEK_SET) < 0) {
		perror("seek error on NES slot");
		return -1;
	}
	ret = read(nes_fd, buf, size);
	if (ret < 0)
		perror("read error on NES slot");
	return ret;
}

int nes_write(uint16_t addr, void *buf, uint16_t size, int mode)
{
	int ret;
	uint32_t a = (mode << 24) + addr;
	DEBUG("addr=%04x data=%04x mode=%d", addr, size, mode);
	if (nes_open() < 0)
		return -1;
	DEBUG("addr=%08x", a);
	if (lseek(nes_fd, a, SEEK_SET) < 0) {
		perror("seek error on NES slot");
		return -1;
	}
	ret = write(nes_fd, buf, size);
	if (ret < 0)
		perror("write error on NES slot");
	return ret;
}

int snes_open(void)
{
	DEBUG("");
	if (snes_fd < 0) {
		snes_fd = open("/dev/slot-md", O_RDWR);
		if (snes_fd < 0) {
			perror("no cart in SNES slot");
			return -1;
		}
	}
	return md_fd;
}

int snes_close(void)
{
	DEBUG("");
	if (snes_fd >= 0) {
		if (close(snes_fd) < 0) {
			perror("can't close SNES slot");
			return -1;	/* unexpected error */
		}
	}
	snes_fd = -1;
	return 0;
}

int snes_read(uint8_t bank, uint16_t addr, void *buf, uint16_t size, int mode)
{
	int ret;
	uint32_t a = (mode << 24) + (bank << 16) + addr;
	DEBUG("bank=%02x addr=%04x data=%04x mode=%d", bank, addr, size, mode);
	if (md_open() < 0)
		return -1;
	DEBUG("addr=%08x", a);
	if (lseek(snes_fd, a, SEEK_SET) < 0) {
		perror("seek error on SNES slot");
		return -1;
	}
	ret = read(snes_fd, buf, size);
	if (ret < 0)
		perror("read error on SNES slot");
	return ret;
}

int snes_write(uint8_t bank, uint16_t addr, void *buf, uint16_t size, int mode)
{
	int ret;
	uint32_t a = (mode << 24) + (bank << 16) + addr;
	DEBUG("bank=%02x addr=%04x data=%04x mode=%d", bank, addr, size, mode);
	if (md_open() < 0)
		return -1;
	DEBUG("addr=%08x", a);
	if (lseek(snes_fd, a, SEEK_SET) < 0) {
		perror("seek error on SNES slot");
		return -1;
	}
	ret = write(snes_fd, buf, size);
	if (ret < 0)
		perror("write error on SNES slot");
	return ret;
}


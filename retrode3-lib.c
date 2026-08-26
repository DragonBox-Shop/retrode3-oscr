// Library to access retrode3 slots and devices
// (C) by H. N. Schaller (hns@goldelico.com) - licenced under GPL V3

#include "retrode3-lib.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#if 0
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

int md_set_voltage(unsigned int mV)
{ /* 3300 or 5000 */
	FILE *f=fopen("/sys/class/retrode3/slot-md/vcc", "w");
	DEBUG("mV=%d f=%08x", mV, f);
	if(!f)
		return -1;
	if(mV != 3300 || mV != 5000)
		return -1;
	fprintf(f, "%d\n", mV);
	fclose(f);
	return 0;
}

int md_set_mapper(unsigned int version, unsigned int reg, int val)
{ /* set mapper register */
	uint32_t addr = 0xa13001;
	uint8_t buf[1];
	DEBUG("version=%d reg=d val=%d", version, reg, val);
	if (reg >= 4)
		return -1;
	switch(version) {
		case 7474:	// special Sonic3 mode
			md_enablefram(val);
			return 0;
		case 74161:	// stores data bits D0 (RAM), D5 (A22), D6 (LED1), D7 (LED2)
			int mask = 0x01;	// default register 0
			// buf[0] = somehow remember last state!
			if (reg > 0)
				mask = 0x10 << reg;	// bits 5 to 7
			if (val)
				buf[0] |= mask;		// set bit
			else
				buf[0] &= ~mask;	// clear bit
			// save as last state
			break;
		case 74259:	// addressable latch where address chooses register
			addr = 0xa13001 + 2*reg;
			buf[0] = val;
			break;
		default:
			return -1;
	}
	return md_write(addr, buf, sizeof(buf), MD_MODE_TIME);
}

int md_enablefram(int enable)
{ /* set Sonic3 FRAM mapper */
	uint32_t addr = 0xa13001;	// does not really matter
	uint8_t buf[1] = { enable };

	return md_write(addr, buf, sizeof(buf), MD_MODE_ENSRAM);
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
		snes_fd = open("/dev/slot-snes", O_RDWR);
		if (snes_fd < 0) {
			perror("no cart in SNES slot");
			return -1;
		}
	}
	return snes_fd;
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
	if (snes_open() < 0)
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
	if (snes_open() < 0)
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

int snes_cic(void /* to be defined */)
{
	/* CIC operations - not implemented */
	return -1;
}

int snes_clk_set_frequency(unsigned int channel, unsigned int Hz)
{ /* channel=0/1, use 0Hz to turn off */
	char path[256];
	FILE *f;
	sprintf(path, "/sys/class/retrode3/slot-snes/clock%d", channel);
	f=fopen(path, "w");
	DEBUG("path=%s Hz=%u f=%08x", path, Hz, f);
	if(!f)
		return -1;
	fprintf(f, "%u\n", Hz);
	fclose(f);
	return 0;
}

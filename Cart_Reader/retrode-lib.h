// Library to access retrode3 slots and devices
// (C) by H. N. Schaller (hns@goldelico.com) - licenced under GPL V3

#ifndef RETRODE_LIB_H
#define RETRODE_LIB_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// call these from within the patched MD.ino
// or some external tool

/* must match kernel driver */
// use bit patterns so that we can mix and enable/disable features?
// e.g. MODE_TIME does additional TIME pulse, WRITE_WITHOUT_WE, WITH_RESET
#define MD_MODE_ROM			0	// default read/write
#define MD_MODE_P10			1	// 10 toggle pulses on CLK
#define MD_MODE_P1			2	// 1 toggle pulse on CLK
#define MD_MODE_TIME		3	// address with TIME impulse with WE
#define MD_MODE_FLASH		undefined?
#define MD_MODE_ENSRAM		5	// TIME impulse without WE
#define MD_MODE_EEPMODE		6	// ?

/* wrappers for direct access */
int open_md(void);	/* select MD slot */
int close_md(void);	/* deselect MD slot */
int read_md(unsigned long addr, void *buf, int size, int mode);
int write_md(unsigned long, void *buf, int size, int mode);
int set_voltage_md(int mV);	/* 3300 or 5000 */

// more wrappers for SNES, NES
// also for reading/writing RAM in different modes

// the goal is to hide all kernel interfaces from user space
// and to allow to write plugins for cards that are plugged in through adapters

int open_Si5351(void);
int close_Si5351(void);
int set_freq_Si5351(int Hz);

#ifdef __cplusplus
}
#endif

#endif // !RETRODE_LIB_H

// Library to access retrode3 slots and devices
// (C) by H. N. Schaller (hns@goldelico.com) - licenced under GPL V3

#ifndef RETRODE_LIB_H
#define RETRODE_LIB_H

#include <stdint.h>
#include <stdbool.h>

// call these from within the patched MD.ino
// or some external tool

/* wrappers for direct access */
int read_md(uint32_t addr, uint16_t size, uint16_t *buf);
int write_md(uint32_t addr, uint16_t size, uint16_t *buf);
int time_read_md(uint32_t addr, uint16_t size, uint16_t *buf);
int time_md(bool on_off);	/* activate/deactivate time */
int reset_md(bool on_off);	/* activate/deactivate reset */
int select_md(bool on_off);	/* activate/deactivate cart enable */

// more wrappers for SNES, NES
// also for reading/writing RAM in different modes

// the goal is to hide all kernel interfaces from user space
// and to allow to write plugins for cards that are plugged in through adapters

#endif // !RETRODE_LIB_H

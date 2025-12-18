// Library to access retrode3 slots and devices
// (C) 2025 by H. N. Schaller (hns@goldelico.com) - licenced under GPL V3

#ifndef RETRODE_LIB_H
#define RETRODE_LIB_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* NOTES:
 *   megadrive has 24 bit wide are byte addresses
 *   the driver attempts to read/write words where possible
 */

#define MD_MODE_ROM			0	// default read/write
#define MD_MODE_P10			1	// 10 toggle pulses on CLK
#define MD_MODE_P1			2	// 1 toggle pulse on CLK
#define MD_MODE_TIME		3	// address with TIME impulse with WE
#define MD_MODE_FLASH		undefined?
#define MD_MODE_ENSRAM		5	// TIME impulse without WE
#define MD_MODE_EEPMODE		6	// ?

int md_open(void);	/* select MD slot */
int md_close(void);	/* deselect MD slot */
int md_read(uint32_t addr, void *buf, uint32_t size, int mode);
int md_write(uint32_t addr, void *buf, uint32_t size, int mode);
int md_set_voltage(int mV);	/* 3300 or 5000 */

#define NES_MODE_PRG		10	// CPU ROM: D0..D7
#define NES_MODE_CHR		11	// PPU ROM: D8..D15
#define NES_MODE_CHR_M2		12
#define NES_MODE_MMC5_SRAM	13 	// CPU ROM: D0..D7
#define NES_MODE_REG		14	// xE000/0xF000
#define NES_MODE_RAM		15	// CPU RAM:
#define NES_MODE_WRAM		16	// CART RAM: D0..D7

int nes_open(void);	/* select MD slot */
int nes_close(void);	/* deselect MD slot */
int nes_read(uint16_t addr, void *buf, uint16_t size, int mode);
int nes_write(uint16_t addr, void *buf, uint16_t size, int mode);

#define SNES_MODE_REGULAR	0
#define SNES_MODE_HIROM		9

int snes_open(void);	/* select MD slot */
int snes_close(void);	/* deselect MD slot */
int snes_read(uint8_t bank, uint16_t addr, void *buf, uint16_t size, int mode);
int snes_write(uint8_t bank, uint16_t addr, void *buf, uint16_t size, int mode);

int snes_cic(void /* to be defined */);		/* CIC operations - not implemented */
int snes_clk_set_frequency(int num, int Hz);	/* clk=0/1/2, use 0Hz to turn off - not implemented */

#ifdef __cplusplus
}
#endif

#endif // !RETRODE_LIB_H

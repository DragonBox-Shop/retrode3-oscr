/*
 * tool for testing kernel and NES readout with different mappers
 * it is based on a simplified version of NES.ino
 *
 * gcc -std=c99 -o nes-test -I/usr/include -I/usr/local/include -L/usr/local/lib nes-test.c /usr/local/lib/retrode3-lib.o
 */

#include <stdio.h>
#include <stdlib.h>
#include <retrode3-lib.h>

typedef uint8_t byte;
typedef uint16_t word;

char *arg0;
int mapper;
uint8_t sdBuffer[512];

void hex(uint8_t *buf, uint16_t size)
{
	while(size-- > 1)
		printf("%02x ", *buf++);
	printf("%02x\n", *buf++);
}

static unsigned char read_prg_byte(unsigned int address) {
	byte myData;
	nes_read(address, &myData, sizeof(myData), NES_MODE_PRG);
	return myData;
}

static unsigned char read_chr_byte(unsigned int address) {
	byte myData;
	nes_read(address, &myData, sizeof(myData), NES_MODE_CHR);
	return myData;
}

static void write_reg_byte(unsigned int address, uint8_t data) {  // FIX FOR MMC1 RAM CORRUPTION
	nes_write(address, &data, sizeof(data), NES_MODE_REG);
}

static void write_prg_byte(unsigned int address, uint8_t data) {
	nes_write(address, &data, sizeof(data), NES_MODE_PRG);
	return;
}

void write_mmc1_byte(unsigned int address, uint8_t data) {  // write loop for 5 bit register
	if (address >= 0xE000) {
		for (uint8_t i = 0; i < 5; i++) {
			write_reg_byte(address, data >> i);  // shift 1 bit into temp register [WRITE RAM SAFE]
		}
	} else {
		for (uint8_t j = 0; j < 5; j++) {
			write_prg_byte(address, data >> j);  // shift 1 bit into temp register
		}
	}
}

void dumpPRG(word base, word address) {
	nes_read(base + address, sdBuffer, 32, NES_MODE_PRG);
	hex(sdBuffer, 32);
}

void dumpCHR(word address) {
	for (size_t x = 0; x < 32; x++) {
		sdBuffer[x] = read_chr_byte(address + x);
	}
	hex(sdBuffer, 32);
}

void dumpBankPRG(const size_t from, /*const*/ size_t to, const size_t base) {
	to = from + 0x20;	// limit here...
	for (size_t address = from; address < to; address += 512) {
		dumpPRG(base, address);
	}
}

void dumpBankCHR(const size_t from, /*const*/ size_t to) {
	to = from + 0x20;	// limit here...
	for (size_t address = from; address < to; address += 512) {
		dumpCHR(address);
	}
}

uint64_t int_pow(uint64_t base, unsigned int exp)
{
	uint64_t result = 1;
	while (exp) {
		if (exp & 1)
			result *= base;
		exp >>= 1;
		base *= base;
	}
	return result;
}

void readPRG(bool readrom)
{
	int prgsize = 2;
	uint16_t banks;
	int base = 0x8000;
	bool busConflict = false;

	switch(mapper) {
		case 0:
		case 3:
		case 13:
			banks = int_pow(2, prgsize);
			dumpBankPRG(0x0, 0x4000 * banks, base);
			break;
		case 1:
			if (prgsize == 1) {
				write_prg_byte(0x8000, 0x80);
				dumpBankPRG(0x0, 0x8000, base);
			} else {
				banks = int_pow(2, prgsize);
				for (size_t i = 0; i < banks; i++) {  // 16K Banks ($8000-$BFFF)
					printf("bank = %d\n", i);
					write_prg_byte(0x8000, 0x80);       // Clear Register
					write_mmc1_byte(0x8000, 0x0C);      // Switch 16K Bank ($8000-$BFFF) + Fixed Last Bank ($C000-$FFFF)
					if (prgsize > 4)                    // 512K
						write_mmc1_byte(0xA000, 0);       // Reset 512K Flag for Lower 256K
					if (i > 15)                         // Switch Upper 256K
						write_mmc1_byte(0xA000, 0x10);    // Set 512K Flag
					write_mmc1_byte(0xE000, i);
					dumpBankPRG(0x0, 0x4000, base);
				}
			}
			break;
		case 2:   // bus conflicts - fixed last bank
			banks = int_pow(2, prgsize);
			busConflict = true;
			for (size_t i = 0; i < banks; i++) {
				for (size_t x = 0; x < 0x8000; x++) {
					if (read_prg_byte(0xC000 + x) == i) {
						write_prg_byte(0xC000 + x, i);
						busConflict = false;
						break;
					}
				}
				if (busConflict) {
					write_prg_byte(0xC000 + i, i);
				}
				dumpBankPRG(0x0, 0x4000, base);
			}
		case 4:
		case 12:
			banks = int_pow(2, prgsize) * 2;
			write_prg_byte(0xA001, 0x80);  // Block Register - PRG RAM Chip Enable, Writable
			for (size_t i = 0; i < banks; i++) {
				printf("bank = %d\n", i);
				write_prg_byte(0x8000, 0x06);  // PRG Bank 0 ($8000-$9FFF)
				write_prg_byte(0x8001, i);
				dumpBankPRG(0x0, 0x2000, base);
			}
			break;
		case 5:  // 128K/256K/512K
			banks = int_pow(2, prgsize) * 2;
			write_prg_byte(0x5100, 3);               // 8K PRG Banks
			for (size_t i = 0; i < banks; i += 2) {  // 128K/256K/512K
				write_prg_byte(0x5114, i | 0x80);
				write_prg_byte(0x5115, (i + 1) | 0x80);
				dumpBankPRG(0x0, 0x4000, base);
			}
			break;
		case 7:   // 128K/256K
			banks = int_pow(2, prgsize) / 2;
			for (size_t i = 0; i < banks; i += 1) {  // 8K/16K/32K/64K/128K (Bank #s are based on 4K Banks)
				write_prg_byte(0x8003, i);          // write mapper to choose bank
				printf("bank = %d\n", i);
				dumpBankPRG(0x0, 0x8000, base);
			}
			break;
		case 9:
			banks = int_pow(2, prgsize) * 2;  // 8K banks
			for (size_t i = 0; i < banks; i++) {
				write_prg_byte(0xA000, i);       // Switch bank at $8000
				dumpBankPRG(0x0, 0x2000, base);  //
			}
			break;
		case 10:
			banks = int_pow(2, prgsize);
			for (size_t i = 0; i < banks; i++) {
				write_prg_byte(0xA000, i);
				dumpBankPRG(0x0, 0x4000, base);
			}
			break;
		case 11:
			banks = int_pow(2, prgsize) / 2;
			for (size_t i = 0; i < banks; i++) {
				write_prg_byte(0xFFB0 + i, i);
				dumpBankPRG(0x0, 0x8000, base);
			}
			break;
		case 15:
			banks = int_pow(2, prgsize);
			for (size_t i = 0; i < banks; i += 2) {
				write_prg_byte(0x8000, i);
				dumpBankPRG(0x0, 0x8000, base);
			}
			break;
		default:
			fprintf(stderr, "%s: unknown mapper %d\n", arg0, mapper);
			exit(1);
	}
}

int main(int argc, char *argv[]) {
	arg0 = argv[0];
	mapper = argv[1] ? atoi(argv[1]) : -1;
	readPRG(mapper);

	return 0;
}

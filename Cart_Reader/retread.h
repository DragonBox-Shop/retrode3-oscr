// Compatibility-Header to be able to compile the code under Linux

#ifndef RETREAD_H
#define RETREAD_H

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>	// for O_RDONLY
#define O_READ O_RDONLY	// as used in code
#include <math.h>	// for log()
#include <cstring>
#include <ctype.h>	// for isprint()
// #include <bsd/string.h>	// for strlcpy()

/*** modules to compile ***/

#define ENABLE_MD
#define ENABLE_NES
#define ENABLE_SNES

/*** global types ***/

typedef bool boolean;
typedef unsigned char byte;
typedef unsigned long dword;
typedef unsigned short word;

typedef char *String;

/*** code qualifiers ***/

#define PROGMEM
#define pgm_read_byte(addr) (*(byte *) (addr))
#define pgm_read_dword(addr) ((uintptr_t)*(dword *) (addr))
#define pgm_read_word(addr) ((uintptr_t)*(word *) (addr))
#define PSTR(string) string	// we do not need to distinguish memory and program memory
#define __asm__(...)	// eliminate any assembler code since our hardware is handled by kernel driver

/*** C++ objects defned in retread.cpp ***/

class __FlashStringHelper;
#define F(string_literal) (reinterpret_cast<const __FlashStringHelper *>(PSTR(string_literal)))

extern class Display {
public:
	void setCursor(int x, int y);
} display;

extern class FsFile {
public:
	operator bool() { return available(); }
	bool available();
	bool isDir();
	bool isFile();
	bool isHidden();
	bool getName(char *name, int maxlen);
	bool open(const char *path);
	bool open(const char *path, int flags);
	bool openNext(FsFile *dir, int flags);
	bool rename(const char *name);
	void write(const byte *buffer, int size);
	void write(char *buffer, int size);
	void write(byte value);
	size_t read(byte *buffer, int size);
	size_t read(char *buffer, int size);
	byte read();
	char peek();
	void seek(unsigned long offset);
	void rewind();
	void seekCur(long offset);
	unsigned long curPosition();
	size_t fileSize();
	void close();
	void flush();
} myFile;

extern class SdFs {
public:
	bool begin(int unknown);
#define SS 0	// used as parameter for begin()
	void mkdir(const char dir[38], bool flag);
	void chdir(const char *dir);
	void chdir();
	FsFile open(char *path, int flags);
	bool exists(char *path);
} sd;

extern class Serial {
public:
	void begin(int baudrate);
	void print(const __FlashStringHelper *str);
	void println(const __FlashStringHelper *str);
	void println();
	bool available();
	byte read();
	char *readStringUntil(char *until);
} Serial;
// ClockedSerial

extern class EEPROM {
public:
	byte read(int addr);
	void write(int addr, byte value);
	void println();
} EEPROM;

const int SI5351_CLK0 = 0;
const int SI5351_CLK1 = 1;
const int SI5351_CLK2 = 2;
const int SI5351_CRYSTAL_LOAD_8PF = 8;

extern class Si5351 {
public:
	bool init(int load, int param2, int param3);
	void output_enable(int clockport, bool onoff);
	void set_freq(unsigned long freq, int clockport);
	void update_status();
} clockgen;

/*** global variables from Cart_reader.ino ***/

extern bool i2c_found;
extern int foldern;
extern unsigned long writeErrors;
extern int choice;

#define FILENAME_LENGTH 100
#define FILEPATH_LENGTH 132
#define FILEOPTS_LENGTH 20

extern char fileName[FILENAME_LENGTH];
extern char filePath[FILEPATH_LENGTH];
extern byte currPage;
extern byte lastPage;
extern byte numPages;
extern boolean root;
extern boolean filebrowse;
extern char romName[22];
extern unsigned long sramSize;
extern int romType;
extern byte saveType;
extern word romSize;
extern word numBanks;
extern char checksumStr[9];
extern bool errorLvl;
extern byte romVersion;
extern char cartID[5];
extern unsigned long cartSize;
extern unsigned int flashid;
extern char flashid_str[5];
extern char vendorID[5];
extern unsigned long fileSize;
extern unsigned long sramBase;
extern unsigned long flashBanks;
extern bool flashX16Mode;
extern bool flashSwitchLastBits;
// extern enum CORES: uint8_t mode;
extern byte sdBuffer[512];
// used by MD and NES modules
extern byte eepbit[8];
extern byte eeptemp;
extern void setup_NES();
extern void getMapping();
extern void checkStatus_NES();
extern void nesMenu();
extern void snsMenu();
extern void mdMenu();
extern void snesMenu();
extern void mdCartMenu();
extern void segaCDMenu();

/*** misc definitions ***/
#define HEX 16

/*** system functions ***/

#define delay(msec)	// ignore
#define _delay_us(usec)	// ignore
#define delayMicroseconds(usec)	// ignore
#define memcpy_P(to, fm, sz) memcpy(to, fm, sz)
#define strcpy_P(to, fm) strcpy(to, fm)
extern size_t strlcpy(char *dst, const char *src, size_t size);
#define strlcpy_P(to, fm, sz) strlcpy(to, fm, sz)
#define snprintf_P(filename, size, format, prefix, ...) snprintf(filename, size, format, prefix, __VA_ARGS__)
#define analogWrite(num, r)
extern char *itoa(unsigned long value, char str[], int radix);

/*** direct hardware port access (effectively disabled for read&write) ***/

unsigned char dummy;
#define PORTA	dummy	// PORTA = value; assigns to dummy variable so that compiler can optimize away
#define PINA	0		// always read input as 0 so that compiler can optimize away
#define PORTC	dummy
#define PINC	0
#define PORTE	dummy
#define PORTF	dummy
#define PORTG	dummy
#define PORTH	dummy
#define PORTJ	dummy
#define PORTK	dummy
#define PINK	0
#define PORTL	dummy
#define DDRA	dummy
#define DDRB	dummy
#define DDB4	4
#define DDB5	5
#define DDB6	6
#define DDRC	dummy
#define DDRD	dummy
#define DDRE	dummy
#define DDRF	dummy
#define DDRG	dummy
#define DDRH	dummy
#define DDRJ	dummy
#define DDRK	dummy
#define DDRL	dummy

#endif

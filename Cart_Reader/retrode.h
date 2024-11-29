// Compatibility-Header to be able to compile the code under Linux
// (C) by H. N. Schaller (hns@goldelico.com) - licenced under GPL V3

#ifndef RETRODE_H
#define RETRODE_H

#define time imported_time	// names will be used for local variables
#define clock imported_clock

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>	// for O_RDONLY
#include <math.h>	// for log()
#include <ctype.h>	// for isprint()
// #include <bsd/string.h>	// for strlcpy()
#include <arpa/inet.h>	// for htons() and ntohs()

#include <cstring>

#undef time
#undef clock

/*** modules to compile ***/

#define ENABLE_MD	// Megadrive
#define ENABLE_NES	// NES
#define ENABLE_SNES	// SNES
#define ENABLE_FLASH	// SNES/MD Flashing - does not yet compile (epromMenu, setup_Flash8, flashSize, blank and more are missing) - needs FLASH.ino
#define startCFIMode(x)	// is defined in GB.ino and used by FLASH.ino

#define ENABLE_CONFIG

#define ENABLE_VSELECT
#define HW5	// Retrode3 has a different hardware versioning system so fake to have HW5

#define ENABLE_SERIAL	// we use the Serial interface (mapped to stdin and stdout)
// #define ENABLE_LCD	// pretend to have an LCD

/*** global types ***/

typedef bool boolean;
typedef uint8_t byte;
typedef uint16_t word;
typedef uint32_t dword;

/*** code qualifiers ***/

#define PROGMEM
#define pgm_read_byte(addr) (*(byte *) (addr))
#define pgm_read_word(addr) (*(word *) (addr))
#define pgm_read_dword(addr) (*(dword *) (addr))
#define PSTR(string) string	// we do not need to distinguish memory and program memory
#define __asm__(...)	// eliminate any assembler code since our hardware is handled by kernel driver
#define asm	// wipe out in GBS.ino, MD.ino, WS.ino
#define volatile(...)

/*** C++ objects defined in retrode.cpp ***/

class __FlashStringHelper;
#define F(string_literal) (reinterpret_cast<const __FlashStringHelper *>(PSTR(string_literal)))

class String {
private:
	char *str;
public:
	String();
	String(char *);
        ~String();
	int toInt();
	char *toCstring();
};

extern class Display {
public:
	void setCursor(int x, int y);
} display;

#define O_READ O_RDONLY		// Arduino special
#define O_WRITE O_WRONLY	// Arduino special

extern class FsFile {
private:
	const char *path;
	FILE *file;
public:
	FsFile();
	FsFile(char *path);
	operator bool() { return file != NULL; }
	bool exists();
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
	size_t available();
	size_t read(byte *buffer, int size);
	size_t read(char *buffer, int size);
	size_t readBytesUntil(char end, char *buffer, int size);
	byte read();
	char peek();
	void seek(off_t offset);
	void rewind();
	void seekCur(off_t offset);
	off_t curPosition();
	off_t fileSize();
	void close();
	void flush();
} myFile;

extern class SdFs {
public:
	bool begin(int unknown);
#define SS 0	// used as parameter for begin()
	void mkdir(const char *dir, bool flag);
	void chdir(const char *dir);
	void chdir();
	FsFile open(char *path, int flags);
	bool exists(char *path);
} sd;

extern class Serial {
public:
	void begin(int baudrate);
	void print(String str);
	void print(const char *);
	void print(int);
	void print(unsigned int);
	void print(long unsigned int);
	void print(const __FlashStringHelper *);
	void print(byte, int);
	void print(word, int);
	void print(int, int);
	void print(long unsigned int, int);
	void println(String str);
	void println();
	void println(const char *);
	void println(long unsigned int);
	void println(const __FlashStringHelper *);
	void println(byte, int);
	size_t available();
	byte read();
	String readStringUntil(char until);
} Serial;
// ClockedSerial

extern class EEPROM {
private:
	int fd;
public:
	EEPROM();
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

/*** from FLASH.ino ***/

extern unsigned long flashSize;
extern byte flashromType;
extern byte secondID;
extern unsigned long time;
extern unsigned long blank;
extern unsigned long sectorSize;
extern uint16_t bufferSize;
extern byte mapping;

/*** from Config.h ***/

#define CONFIG_FILE "config.txt"
#define CONFIG_KEY_MAX 32
#define CONFIG_VALUE_MAX 32

/*** system functions ***/

#define delay(msec)	// ignore
#define _delay_us(usec)	// ignore
#define delayMicroseconds(usec)	// ignore
#define interrupts()	// ignore
#define noInterrupts()	// ignore
#define millis() 0	// well...

#define memcpy_P(to, fm, sz) memcpy(to, fm, sz)
#define strcpy_P(to, fm) strcpy(to, fm)
extern size_t strlcpy(char *dst, const char *src, size_t size);
#define strlcpy_P(to, fm, sz) strlcpy(to, fm, sz)
#define snprintf_P(filename, size, format, prefix, ...) snprintf(filename, size, format, prefix, __VA_ARGS__)
#define analogWrite(num, r)
extern char *itoa(unsigned long value, char str[], int radix);
#define DEC 10
#define HEX 16
extern uint8_t checkButton(); // only with ENABLE_LCD or ENABLE_OLED
extern int navigateMenu(int min, int max, void (*printSelection)(int)); // only with ENABLE_LCD or ENABLE_OLED

/*** direct hardware port access (effectively disabled for read&write) ***/

extern byte dummy;
#define PORTA	dummy	// PORTA = value; assigns to dummy variable so that compiler can optimize away
#define PINA	0		// always read input as 0 so that compiler can optimize away
#define PORTB	dummy
#define PORTC	dummy
#define PINC	0
#define PORTD	dummy
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

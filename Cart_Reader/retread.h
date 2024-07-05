// Compatibility-Header to be able to compile the code under Linux

#ifndef RETREAD_H
#define RETREAD_H

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>	// for O_RDONLY
#define O_READ O_RDONLY
#include <math.h>

#include <cstring>

/*** modules to compile ***/

#define ENABLE_MD
#define ENABLE_SNES
#define ENABLE_NES

/*** global types ***/
#define boolean bool
#define byte unsigned char
#define word unsigned short
// Fixme: this is also used as word(something) i.e. function like

/*** code qualifiers ***/
#define PROGMEM
#define __FlashStringHelper char
#define NOP
#define __asm__(...)

/*** C++ objects ***/

class Display {
public:
	void setCursor(int x, int y);
} display;

class FsFile {
public:
	bool available();
	bool open(const char *path, int flags);
	void write(byte *buffer, int size);
	void write(byte value);
	void read(byte *buffer, int size);
	byte read();
	void seekCur(long offset);
	size_t fileSize();
	void close(void);
} myFile;

class SdFs {
public:
	void chdir(const char *dir);
	void chdir();
	FsFile open(char *path, int flags);
	bool exists(char *path);
} sd;

class Serial {
public:
	void print();
	void println();
} serial;

const int SI5351_CLK0 = 0;
const int SI5351_CLK1 = 1;
const int SI5351_CLK2 = 2;

class ClockGen {
public:
	void output_enable(int clockport, bool onoff);
	void set_freq(unsigned long freq, int clockport);
	void update_status();
} clockgen;

/*** global variables from Cart_reader.ino ***/
bool i2c_found;
int foldern;
unsigned long writeErrors;
int choice = 0;

#define FILENAME_LENGTH 100
#define FILEPATH_LENGTH 132
#define FILEOPTS_LENGTH 20

char fileName[FILENAME_LENGTH];
char filePath[FILEPATH_LENGTH];
byte currPage;
byte lastPage;
byte numPages;
boolean root = 0;
boolean filebrowse = 0;
char romName[22];
unsigned long sramSize = 0;
int romType = 0;
byte saveType;
word romSize = 0;
word numBanks = 128;
char checksumStr[9];
bool errorLvl = 0;
byte romVersion = 0;
char cartID[5];
unsigned long cartSize;
unsigned int flashid;
char flashid_str[5];
char vendorID[5];
unsigned long fileSize;
unsigned long sramBase;
unsigned long flashBanks;
bool flashX16Mode;
bool flashSwitchLastBits;
enum CORES {
	CORE_SNES,
	CORE_MD_CART,
	CORE_SEGA_CD,
	CORE_NES,
	CORE_MAX,
};
CORES mode = CORE_MAX;
byte sdBuffer[512];
// used by MD and NES modules
byte eepbit[8];
byte eeptemp;

/*** user interface ***/
#define fileBrowser(string)
#define display_Clear()
#define display_Update()
#define draw_progressbar(value, total)
#define print_STR(string, newline) printf(newline?"%s\n":"%s", string)

/// die haben alle nur 1 Paramter. Entweder eine Zahl oder F("string") oder FS(FSTRING_...)
// einmal gibts printlnMsg(String(cal_factor)) was ein C++ Konstrukt ist!
// ein anderes mal print_Msg(cartSizeLockon * 8 / 1024 / 1024)
// d.h. das geht nur in C++ nachzubauen!!!

#define print_Msg(FMT, ...) printf(FMT __VA_ARGS__ )
#define println_Msg(FMT, ...) printf(FMT "\n" __VA_ARGS__ )
#define print_Error(FMT, ...) fprintf(stderr, FMT __VA_ARGS__ )
#define print_FatalError(FMT, ...) fprintf(stderr, FMT __VA_ARGS__ )
#define print_Msg_PaddedHexByte(hex)
#define print_Msg_PaddedHex16(hex)
#define print_Msg_PaddedHex32(hex)
#define convertPgm(...)

// FIXME: make this global function and move implementation to retread.cpp
char menuOptions[7][20];
static unsigned char question_box(const __FlashStringHelper* question, char answers[7][20], uint8_t num_answers, uint8_t default_choice){
	return 1;
}
static int navigateMenu(int min, int max, void (*printSelection)(int))
{
	return 1;
}

/*** string constants ***/
#define F(string) string
#define FS(string) string

#define FSTRING_RESET "Reset"
#define FSTRING_OK "OK"
#define FSTRING_EMPTY ""
#define FSTRING_SPACE " "
#define FSTRING_CURRENT_SETTINGS "Current Settings"
#define FSTRING_MODULE_NOT_ENABLED "Module is not enabled."
#define FSTRING_DATABASE_FILE_NOT_FOUND "Database file not found"
#define FSTRING_FILE_DOESNT_EXIST "File doesn't exist"
#define FSTRING_READ_ROM "Read ROM"
#define FSTRING_READ_SAVE "Read Save"
#define FSTRING_WRITE_SAVE "Write Save"
#define FSTRING_SELECT_CART "Select Cart"
#define FSTRING_SELECT_CART_TYPE "Select Cart Type"
#define FSTRING_SELECT_FILE "Select File"
#define FSTRING_SET_SIZE "Set Size"
#define FSTRING_REFRESH_CART "Refresh Cart"
#define FSTRING_MAPPER "Mapper: "
#define FSTRING_SIZE "Size: "
#define FSTRING_NAME "Name: "
#define FSTRING_CHECKSUM "Checksum: "
#define FSTRING_ROM_SIZE "ROM Size: "
#define FSTRING_REVISION "Revision: "
#define FSTRING_SERIAL "Serial: "

// ... more in OSCR.cpp
/* original Cart_reader.ino uses numbers and a table but we have enough space to directly use strings */
#define press_button_STR	"Press Button..."
#define sd_error_STR		"SD Error"
#define reset_STR		FSTRING_RESET
#define did_not_verify_STR	"did not verify"
#define _bytes_STR 		" bytes "
#define error_STR		"Error: "
#define create_file_STR		"Can't create file"
#define open_file_STR		"Can't open file"
#define file_too_big_STR	"File too big"
#define done_STR		"Done"
#define saving_to_STR		"Saving to "
#define verifying_STR		"Verifying..."
#define flashing_file_STR	"Flashing file "
#define press_to_change_STR	"Press left to Change"
#define right_to_select_STR	"and right to Select"
#define rotate_to_change_STR	"Rotate to Change"
#define press_to_select_STR	"Press to Select"

/*** system functions ***/
#define int_pow(TWO, x) (1<<x)	// assumes that we take 2 to some power
#define delay(msec)
#define _delay_us(usec)
#define delayMicroseconds(usec)
#define wait()
#define memcpy_P(to, fm, sz) memcpy(to, fm, sz)
#define snprintf_P(filename, size, format, prefix, ...) snprintf(filename, size, format, prefix, __VA_ARGS__)

/*** general functions ***/
#define setup_Flash8()
#define identifyCFI_Flash()
#define setupFlashVoltage()
#define setVoltage(volt)
#define VOLTS_SET_5V	5000
#define VOLTS_SET_3V3	3300
#define VOLTS_SUCCESS	true
#define VOLTS_ERROR	false
#define VOLTS_NOTENABLED	-1
#define clkcal()
#define print_MissingModule()
#define initializeClockOffset()
#define rgbLed(led)
#define blinkLED()
#define resetArduino()
#define compareCRC(file, arg2, arg3, arg4) /* should be implemented... */
#define createFolder(file, arg2, arg3, arg4) /* should be implemented... */
#define createFolderAndOpenFile(file, arg2, arg3, arg4) /* should be implemented... */
#define EEPROM_writeAnything(reg, var)
#define EEPROM_readAnything(reg, var)
#define pgm_read_word(arg) 0
#define pgm_read_byte(arg) 0

/*** direct hardware port access (effectively disabled for read&write) ***/

unsigned char dummy;
#define PORTA	dummy
#define PINA	0
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
#define DDRC	dummy
#define DDRE	dummy
#define DDRF	dummy
#define DDRG	dummy
#define DDRH	dummy
#define DDRJ	dummy
#define DDRK	dummy
#define DDRL	dummy

/*** Common I/O functions are in kernel driver... */
#define dataIn()
#define dataOut()

#/*** others ***/
// stopSnesClocks_resetCic_resetCart (?)
// controlOut_SNES()	# spezieller syscall oder ioctl
// controlIn_SNES()

#endif
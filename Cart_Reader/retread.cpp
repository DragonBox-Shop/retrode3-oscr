/* new retread driver simulating an Arduino environment for the *.ino files */
// (C) by H. N. Schaller (hns@goldelico.com) - licenced under GPL V3

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>	// for mkdir()
#include "retread.h"
#include "ino.h"	// to know all globals

/*** global objects ***/

byte dummy;		// dummy variable for assigning bytes to PORTs
class Display display;	// global display
class Serial Serial;	// global serial interface
class EEPROM EEPROM;	// global EEPROM

/*** main program ***/

static char *arg0;
static const char *sdroot = ".";		// /media/... ?
static const char *eeprom = "eeprom.bin";	// ~/.eeprom.bin ?

void usage(void)
{
	printf("usage: %s [-h] [-e eeprom.bin] [-r sd-root] command parameters...\n", arg0);
	exit(1);
}

void exit_helper(void)
{
	exit(1);
}

int main(int argc, char *argv[])
{
	extern void (*resetArduino)(void) __attribute__((noreturn));
	resetArduino = &exit_helper;	// will be called at several locations

	arg0=argv[0];
	for(; argv[1] && argv[1][0] == '-'; argv++) {
		switch(argv[1][1])
			{
			case 'e':
				if(argv[1][2])
					eeprom = &argv[1][2];
				else if(argv[2]) {
					eeprom = argv[2];
					argv++;
				}
				else {
					fprintf(stderr, "missing eeprom file name\n");
					usage();
				}
				continue;
			case 'r':
				if(argv[1][2])
					sdroot = &argv[1][2];
				else if(argv[2]) {
					sdroot = argv[2];
					argv++;
				}
				else {
					fprintf(stderr, "missing eeprom file name\n");
					usage();
				}
				continue;
			case 'h':
				usage();
				continue;
			default:
				fprintf(stderr, "unknown option -%c\n", argv[1][1]);
				usage();
				exit(1);
			}
		break;
		}
	setup();	// Arduino setup()
	while(1)
		loop();	// Arduino loop()
	exit(0);

	if(!argv[1]) {
		fprintf(stderr, "missing cart type\n");
		exit(1);
		}
#if 0	// old
#ifdef ENABLE_FLASH
	if(strcmp(argv[1], "flash") == 0)
		flashMenu();
	else
#endif
#ifdef ENABLE_MD
	if(strcmp(argv[1], "megadrive") == 0 || strcmp(argv[1], "md") == 0)
		mdMenu();
	else
#endif
#ifdef ENABLE_NES
	if(strcmp(argv[1], "nes") == 0)
		nesMenu();
	else
#endif
#ifdef ENABLE_SNES
	if(strcmp(argv[1], "snes") == 0)
		snesMenu();
	else
#endif
	{
		fprintf(stderr, "unknown cart type: %s\n", argv[1]);
		usage();
	}
	exit(0);
#endif
}

/*** helper functions from C/C++ standard libraries ***/

extern size_t strlcpy(char *dst, const char *src, size_t size);

size_t strlcpy(char *dst, const char *src, size_t size)
{ /* from kernel.org (nolibc/string.h) */

	size_t len;

	for (len = 0; len < size; len++) {
		dst[len] = src[len];
		if (!dst[len])
			return len;
	}
	if (size)
		dst[size-1] = '\0';

	while (src[len])
		len++;

	return len;
}

/* from https://stackoverflow.com/a/46732059
=============
itoa

Convert integer to string

PARAMS:
- value     A 64-bit number to convert
- str       Destination buffer; should be 66 characters long for radix2, 24 - radix8, 22 - radix10, 18 - radix16.
- radix     Radix must be in range -36 .. 36. Negative values used for signed numbers.
=============
*/

char *itoa(unsigned long value, char str[], int radix)
{
    char        buf [66];
    char	*dest = buf + sizeof(buf);
    boolean     sign = false;

    if (value == 0) {
        memcpy (str, "0", 2);
        return str;
    }

    if (radix < 0) {
        radix = -radix;
        if ( (long long) value < 0) {
            value = -value;
            sign = true;
        }
    }

    *--dest = '\0';

    switch (radix)
    {
    case 16:
        while (value) {
            * --dest = '0' + (value & 0xF);
            if (*dest > '9') *dest += 'A' - '9' - 1;
            value >>= 4;
        }
        break;
    case 10:
        while (value) {
            *--dest = '0' + (value % 10);
            value /= 10;
        }
        break;

    case 8:
        while (value) {
            *--dest = '0' + (value & 7);
            value >>= 3;
        }
        break;

    case 2:
        while (value) {
            *--dest = '0' + (value & 1);
            value >>= 1;
        }
        break;

    default:            // The slow version, but universal
        while (value) {
            *--dest = '0' + (value % radix);
            if (*dest > '9') *dest += 'A' - '9' - 1;
            value /= radix;
        }
        break;
    }

    if (sign) *--dest = '-';

    memcpy (str, dest, buf +sizeof(buf) - dest);
    return str;
}

/*** class implementations mapped to Linux/POSIX ***/

/*** String ***/

String::String()
{
	String((char *) "");
}

String::~String()
{
	// delete[] _text;
	free(str);
}

String::String(char *s)
{
	// alternatively use new char[strlen(s)+1];
	str = (char *) malloc(strlen(s) + 1);
	strcpy(str, s);
}

int String::toInt()
{
	int val;
	sscanf(str, "%d", &val);
	return val;
}

char *String::toCstring()
{
	return str;
};

/*** Display ***/

#if 0	// muss explizit in class...
void Display::Display()
{
	// add constructor code here
}
#endif

void Display::setCursor(int x, int y)
{
printf("%s: add implementation\n", __PRETTY_FUNCTION__);
	return;	// ignore
}

/*** FsFile ***/

// add constructor by file name

FsFile::FsFile()
{
// printf("%s\n", __PRETTY_FUNCTION__);
	path = NULL;
	file = NULL;
}

FsFile::FsFile(char *p)
{
// printf("%s\n", __PRETTY_FUNCTION__);
	path = p;
	file = NULL;
}

bool FsFile::exists()
{
	return file != NULL ? true : false;
}
size_t FsFile::available()
{ // well, should return number of available characters to EOF
// printf("%s\n", __PRETTY_FUNCTION__);
	return file != NULL ? true : false;
}

bool FsFile::isDir()
{
	// stat(path);
printf("%s: add implementation\n", __PRETTY_FUNCTION__);
}

bool FsFile::isFile()
{
	// stat(path);
printf("%s: add implementation\n", __PRETTY_FUNCTION__);
}

bool FsFile::isHidden()
{
printf("%s: add implementation\n", __PRETTY_FUNCTION__);
	return false;
}

bool FsFile::getName(char *name, int maxlen)
{
	return path;
}

bool FsFile::open(const char *p, int flags)
{
printf("%s: %s %04o\n", __PRETTY_FUNCTION__, p, flags);
	int fd = ::open(path = p, flags, 0644);	// -rw-r--r-- for O_CREAT
	if (fd >= 0)
		file = fdopen(fd, "r+");	// reading and writing; creation is controled by O_CREAT
	return file != NULL;
}

bool FsFile::open(const char *path)
{
	return open(path, O_RDWR);
}

bool FsFile::openNext(FsFile *dir, int flags)
{
printf("%s: add implementation\n", __PRETTY_FUNCTION__);
}

bool FsFile::rename(const char *name)
{
printf("%s: add implementation\n", __PRETTY_FUNCTION__);
}

void FsFile::write(const byte *buffer, int size)
{
// printf("%s: add implementation\n", __PRETTY_FUNCTION__);
	fwrite(buffer, 1, size, file);
}
;
void FsFile::write(char *buffer, int size)
{
// printf("%s: add implementation\n", __PRETTY_FUNCTION__);
	fwrite(buffer, 1, size, file);
}

void FsFile::write(byte value)
{
// printf("%s: add implementation\n", __PRETTY_FUNCTION__);
	fputc(value, file);
}

size_t FsFile::read(byte *buffer, int size)
{
printf("%s: add implementation\n", __PRETTY_FUNCTION__);
}

size_t FsFile::read(char *buffer, int size)
{
printf("%s: add implementation\n", __PRETTY_FUNCTION__);
}

size_t FsFile::readBytesUntil(char end, char *buffer, int size)
{
printf("%s: add implementation\n", __PRETTY_FUNCTION__);
}

byte FsFile::read()
{
printf("%s: add implementation\n", __PRETTY_FUNCTION__);
}

char FsFile::peek()
{
printf("%s: add implementation\n", __PRETTY_FUNCTION__);
}

void FsFile::seek(off_t offset)
{
	fseek(file, 0, SEEK_SET);
}

void FsFile::rewind()
{
	::rewind(file);
}

void FsFile::seekCur(off_t offset)
{
	fseek(file, offset, SEEK_CUR);
}

off_t FsFile::curPosition()
{
	return ftell(file);
}

off_t FsFile::fileSize()
{
	// stat
printf("%s: add implementation\n", __PRETTY_FUNCTION__);
}

void FsFile::close()
{
	fclose(file);
	file = NULL;
}

void FsFile::flush()
{
	fflush(file);
}

/*** SdFS ***/

bool SdFs::begin(int unknown)	// called as sd.begin(SS)
{
// printf("%s: add implementation\n", __PRETTY_FUNCTION__);
	return true;
}

void SdFs::mkdir(const char *dir, bool flag)
{ // not clear what "flag" means, but we assume it means "recursive"
printf("%s: %s\n", __PRETTY_FUNCTION__, dir);
// FIXME: prefix with sdroot
// ignore for the moment	::mkdir(dir, 0777);
}

void SdFs::chdir(const char *dir)
{
printf("%s: %s\n", __PRETTY_FUNCTION__, dir);
// FIXME: prefix with sdroot
// ignore for the moment	::chdir(dir);
}

void SdFs::chdir()
{
	chdir("/");
}

FsFile SdFs::open(char *path, int flags)
{
// printf("%s: add implementation\n", __PRETTY_FUNCTION__);
	FsFile f;
// FIXME: prefix with sdroot
	f.open(path, flags);
	return f;
}

bool SdFs::exists(char *path)
{
// printf("%s: add implementation\n", __PRETTY_FUNCTION__);
	FsFile f;
// FIXME: prefix with sdroot
	f.open(path);	// try to open
	return f.exists();
}

/*** Serial ***/

void Serial::begin(int baudrate)
{
	// printf("%s: add implementation\n", __PRETTY_FUNCTION__);
	// ignore
}

void Serial::print(const char *str)
{
	printf("%s", str);
}

void Serial::print(String str)
{
printf("%s: add implementation\n", __PRETTY_FUNCTION__);
}

void Serial::print(int val)
{
	printf("%d", val);
}

void Serial::print(unsigned int val)
{
	printf("%u", val);
}

void Serial::print(long unsigned int val)
{
	printf("%lu", val);
}

void Serial::print(const __FlashStringHelper *str)
{
	printf("%s", str);
	fflush(stdout);	// for progress display
}

void Serial::print(byte val, int more)
{
	printf("%02x", val);
}

void Serial::print(word val, int more)
{
	printf("%04x", val);
}

void Serial::print(int val, int more)
{
	printf("%d", val);
}

void Serial::print(long unsigned int val, int more)
{
	printf("%lu", val);
}

void Serial::println()
{
	printf("\n");
}

void Serial::println(const char *str)
{
	printf("%s\n", str);
}

void Serial::println(String str)
{
printf("%s: add implementation\n", __PRETTY_FUNCTION__);
}

void Serial::println(long unsigned int val)
{
	printf("%lu\n", val);
}

void Serial::println(const __FlashStringHelper *str)
{
	printf("%s\n", str);
}

void Serial::println(byte val, int more)
{
	printf("%02x\n", val);
}

size_t Serial::available()
{ // essentially we want to know if a getc() would block or not
// printf("%s:\n", __PRETTY_FUNCTION__);
#if 0
	fd_set readfds;
	FD_ZERO(&readfds);
	FD_SET(STDIN_FILENO, &readfds);
	fd_set savefds = readfds;

	struct timeval timeout;
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;

	int sel_rv = select(fileno(stdin), &readfds, NULL, NULL, &timeout);
	return sel_rv;
#else
// FIXME: this is likely called in a tight loop
// while (Serial.available() == 0) {}
// which will need 100% CPU load!
// hence we always return 1 here and block in the read() method(s)
// and we block until there is a full line (have to strip off the \n)
	return 1;
#endif
}

byte Serial::read()
{
// FIXME: this is not intuitive after "Press Button..."
// because one has to press space + cr
	int c;
	do {
		c = getc(stdin);
	} while(c == '\b');
	if(c == '\n')
		c = getc(stdin);	// get another character
	return (byte) c;
}

String Serial::readStringUntil(char until)
{ // char is usually '\n'
printf("%s: add implementation\n", __PRETTY_FUNCTION__);
}

/*** EEPROM ***/

#if 0
EEPROM::EEPROM()
{
	// open a file "eeprom.bin"
	// create if needed for 1kB
}
#endif

byte EEPROM::read(int addr)
{
printf("%s: add implementation\n", __PRETTY_FUNCTION__);
}

void EEPROM::write(int addr, byte value)
{
printf("%s: add implementation\n", __PRETTY_FUNCTION__);
}

void EEPROM::println()
{
// what is this doing?
printf("%s: add implementation\n", __PRETTY_FUNCTION__);
}

/*** Si5351 ***/

bool Si5351::init(int load, int param2, int param3)
{
printf("%s: add implementation\n", __PRETTY_FUNCTION__);
	// should control some PWM through /sys
}

void Si5351::output_enable(int clockport, bool onoff)
{
printf("%s: add implementation\n", __PRETTY_FUNCTION__);
	// should control some PWM through /sys
}

void Si5351::set_freq(unsigned long freq, int clockport)
{
printf("%s: add implementation\n", __PRETTY_FUNCTION__);
	// should control some PWM through /sys
}

void Si5351::update_status()
{
printf("%s:\n", __PRETTY_FUNCTION__);
	/* NOP */
}

/*** substitutes for Cart_Reader.ino not available if we have neither ENABLE_LCD nor ENABLE_OLED ***/

#ifndef ENABLE_SERIAL
uint8_t checkButton()
{
printf("%s: add implementation\n", __PRETTY_FUNCTION__);
}
#endif

// where and when is this used at all?

int navigateMenu(int min, int max, void (*printSelection)(int))
{
	int i;
	char c;

printf("%s:\n", __PRETTY_FUNCTION__);
	while(1) { // loop until something is chosen
		i=min;
		while(i < max) {
			printf("%d) ", min);
			printSelection(min++);
		}
		c = getc(stdin);
		// oder ganze Zeile lesen...
		// then take from argv or read choice from stdin
		i = min;
		if(i >= min && i < max)
			return i;
	}
}

/*** EOF ***/

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

static void usage(void)
{
	fprintf(stderr, "usage: cat [-h] [-e eeprom.bin] [-r sd-root] [command parameters ...]\n");
	exit(1);
}

static void error(const char *str)
{
	fprintf(stderr, "%s: %s\n", str);
	usage();
}

static void exit_helper(void)
{
	exit(1);
}

static void help(void)
{
	printf("Usage: cat [-h] [-e eeprom.bin] [-r sd-root] [command parameters ...]\n");
	printf("Options:\n");
	printf("  -h             Display this information\n");
#ifdef ENABLED_EEPROM
	printf("  -e eeprom.bin  Define the eeprom file [default: \"eeprom.bin\"]\n");
#endif
	printf("  -r sd-root     Define the SD root directory [default: \".\"]\n");
	printf("\n");
#ifdef ENABLED_CONFIG
	printf("There is a config file called \"config.txt\".\n");
	printf("\n");
#endif
#ifdef ENABLED_EEPROM
	printf("The eeprom file is used to store e.g.\n");
	printf("  folder number\n");
	printf("  cart mode (NES)\n");
	printf("  mapper (NES)\n");
	printf("  newmapper (NES)\n");
	printf("  prgsize (NES)\n");
	printf("  chrsize (NES)\n");
	printf("  ramsize (NES)\n");
	printf("\n");
#endif
	printf("Menu choices are numbers. Multiple choices of subsequent submenus can be\n");
	printf(" concatenated into a single command\n");
	printf(" Choose the 'Reset' item to exit this tool\n");
	printf("\n");
	printf("Example:\n");
	printf(" echo 200 | %s\n", arg0);
	printf(" some echo commands to use:\n");
	printf("  echo 100      read SNES/SFC cart to file");
	printf("  echo 200      read Megadrive cart to file");
	printf("  echo 210      backup Sega RamCart to file");
}

int main(int argc, char *argv[])
{
	extern void (*resetArduino)(void) __attribute__((noreturn));
	resetArduino = &exit_helper;	// will be called at several locations

	arg0=argv[0];
	for(; argv[1] && argv[1][0] == '-'; argv++) {
		switch(argv[1][1])
			{
#ifdef ENABLED_EEPROM
			case 'e':
				if(argv[1][2])
					eeprom = &argv[1][2];
				else if(argv[2]) {
					eeprom = argv[2];
					argv++;
				}
				else
					error("missing eeprom file name\n");
				continue;
#endif
			case 'r':
				if(argv[1][2])
					sdroot = &argv[1][2];
				else if(argv[2]) {
					sdroot = argv[2];
					argv++;
				}
				else
					error("missing sd root file name\n");
				continue;
			case '?':
			case 'h':
				help();
				exit(0);
			default:
				fprintf(stderr, "%s: unknown option -%c\n", argv[0], argv[1][1]);
				usage();
			}
		break;
		}
	setup();	// Arduino setup()
	while(1)
		loop();	// Arduino loop()
	exit(0);
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
	path = NULL;
	file = NULL;
}

FsFile::FsFile(char *p)
{
	path = p;
	file = NULL;
}

bool FsFile::exists()
{
	return file != NULL ? true : false;
}
size_t FsFile::available()
{
	return fileSize() - curPosition();
}

bool FsFile::isDir()
{
	struct stat buf;
	stat(path, &buf);	// lstat on path?
	return S_ISDIR(buf.st_mode);
}

bool FsFile::isFile()
{
	struct stat buf;
	stat(path, &buf);	// lstat on path?
	return S_ISREG(buf.st_mode);
}

bool FsFile::isHidden()
{
printf("%s: add implementation\n", __PRETTY_FUNCTION__);
	// check if basename starts with '.' ?
	return false;
}

bool FsFile::getName(char *name, int maxlen)
{
	// of real file name only?
	return path;
}

bool FsFile::open(const char *p, int flags)
{
	char wd[128];
printf("%s: '%s' '%s' %04o\n", __PRETTY_FUNCTION__, getcwd(wd, sizeof(wd)), p, flags);
	close();	// may still be open
	path = p;
	int fd = ::open(p, flags, 0644);	// -rw-r--r-- for O_CREAT
	if (fd >= 0) { // hack syscall O_ flags to stdio buffer modes
		const char *mode = "?";
		switch (flags & (O_RDWR | O_RDONLY | O_WRONLY)) {
			case O_RDONLY: mode = "r"; break;
			case O_WRONLY: mode = "w"; break;
			case O_RDWR:
				if (flags & O_CREAT && flags & O_TRUNC)
					mode = "w+";
				else
					mode = "w";
				break;
		}
printf("%s: mode = %s\n", __PRETTY_FUNCTION__, mode);
		file = fdopen(fd, mode);
	} else
		perror(p);
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
printf("%s: '%s' - add implementation\n", __PRETTY_FUNCTION__, name);
}

void FsFile::write(const byte *buffer, int size)
{
	fwrite(buffer, 1, size, file);
}
;
void FsFile::write(char *buffer, int size)
{
	fwrite(buffer, 1, size, file);
}

void FsFile::write(byte value)
{
	fputc(value, file);
}

size_t FsFile::read(byte *buffer, int size)
{
	return fread(buffer, 1, size, file);
}

size_t FsFile::read(char *buffer, int size)
{
	return fread(buffer, 1, size, file);
}

size_t FsFile::readBytesUntil(char end, char *buffer, int size)
{
	int i = 0;
	while(i < size) {
		int c = fgetc(file);
		if (c == EOF)
			break;
		if (c == end)	// this does NOT store the character
			break;
		buffer[i++] = c;
	}
	return i;
}

byte FsFile::read()
{
	return fgetc(file);
}

char FsFile::peek()
{
	int c = fgetc(file);
	ungetc(c, file);
	return c;
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
	struct stat buf;
	fstat(fileno(file), &buf);
	return buf.st_size;
}

void FsFile::close()
{
	if(file)
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

// FIXME: format may be HEX or DEC
void Serial::print(byte val, int format)
{
	if (format == HEX)
		printf("%02x", val);
	else
		printf("%d", val);
}

void Serial::print(word val, int format)
{
	if (format == HEX)
		printf("%04x", val);
	else
		printf("%d", val);
}

void Serial::print(int val, int format)
{
	if (format == HEX)
		printf("%x", val);
	else
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

void Serial::println(byte val, int format)
{
	if (format == HEX)
		printf("%02x\n", val);
	else
		printf("%d\n", val);
}

size_t Serial::available()
{ // essentially we want to know if a getc() would block or not
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
		if (c == EOF)
			exit(0);
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

EEPROM::EEPROM()
{
	fd = open(eeprom, O_RDWR | O_CREAT, 0644);
	if (fd < 0)
		{
		fprintf(stderr, "%s: failed to open %s\n", arg0, eeprom);
		exit(1);
		}
	ftruncate(fd, 1024);	// simulate 1kBx8 EEPROM
}

byte EEPROM::read(int addr)
{
	byte value = 0;
	lseek(fd, addr, SEEK_SET);
	if(::read(fd, &value, sizeof(value)) != sizeof(value))
		fprintf(stderr, "EEPROM read error at address %04x\n", addr);
printf("%s: %04x -> %02x\n", __PRETTY_FUNCTION__, addr, value);
	return value;
}

void EEPROM::write(int addr, byte value)
{
	lseek(fd, addr, SEEK_SET);
	::write(fd, &value, sizeof(value));
printf("%s: %04x <- %02x\n", __PRETTY_FUNCTION__, addr, value);
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

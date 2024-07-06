/* new driver */

#include <stdio.h>
#include <stdlib.h>
#include "retread.h"

/*** global objects ***/

byte dummy;
class Display display;	// global display
class Serial serial;	// global serial interface
class EEPROM EEPROM;	// global EEPROM

/*** main program ***/

static char *arg0;

void usage(void)
{
	printf("usage: %s snes|megadrive|nes command parameters...\n", arg0);
	exit(1);
}

int main(int argc, char *argv[])
{
	arg0=argv[0];
	for(; argv[1] && argv[1][0] == '-'; argv++)
		{
		switch(argv[1][1])
			{
			case ' ':
				continue;
			default:
				fprintf(stderr, "unknown option -%c\n", argv[1][1]);
				exit(1);
			}
		break;
		}
	// decode argv[1] for "snes", "megadrive", "nes"
	// prepare user choices
	// call appropriate subfunctions from the .ino modules
	usage();
	exit(0);
}

/*** helper functions from C/C++ standard libraries ***/

extern size_t strlcpy(char *dst, const char *src, size_t size);

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

#if 0	// muss explizit in class...
void Display::Display()
{
	// add constructor code here
}
#endif

void Display::setCursor(int x, int y)
{
	// add code here
	return;	// ignore
}

#if 0

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

#endif

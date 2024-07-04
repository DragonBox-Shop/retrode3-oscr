// Compatibility-Header to be able to compile the code under Linux

#include <stdlib.h>
#include <stdio.h>

#define boolean bool
#define byte unsigned char
#define word unsigned short

// user interface
#define fileBrowser(string)
#define display_Clear()
#define display_Update()
#define print_STR(FMT, ...) printf(FMT, __VA_ARGS__)
#define print_Msg(FMT, ...) printf(FMT, __VA_ARGS__)
#define println_Msg(FMT, ...) printf(FMT"\n", __VA_ARGS__)
#define print_Error(FMT, ...) fprintf(stderr, FMT, __VA_ARGS__)
#define convertPgm(...)
#define question_box(...)
#define F(string) string
#define FS(string) string
#define FSTRING_READ_ROM "read"
#define FSTRING_READ_SAVE "save"
#define FSTRING_WRITE_SAVE "write"
#define FSTRING_REFRESH_CART "refresh"
#define FSTRING_RESET "reset"
#define FSTRING_OK "ok"
#define FSTRING_EMPTY "empty"
#define error_STR "error: "
#define _bytes_STR "bytes"
#define did_not_verify_STR "did not verify"
#define int_pow(x) (1<<x)

// system
#define setup_Flash8()
#define identifyCFI_Flash()
#define setupFlashVoltage()
#define setVoltage(volt)
#define clkcal()
#define print_MissingModule()
#define initializeClockOffset()
#define rgbLed(led)
#define resetArduino()
#define delay(msec)
#define _delay_us(usec)
#define wait()
#define PROGMEM
#define NOP
#define __asm__(...)

// no direct hardware port access (effectively disables read/write)

unsigned char dummy;
#define PORTC	dummy
#define PORTE	dummy
#define PORTF	dummy
#define PORTG	dummy
#define PORTH	dummy
#define PORTL	dummy
#define PORTK	dummy
#define DDRA	dummy
#define DDRC	dummy
#define DDRE	dummy
#define DDRF	dummy
#define DDRG	dummy
#define DDRH	dummy
#define DDRJ	dummy
#define DDRK	dummy
#define DDRL	dummy

/* solve this by #ifdef __Linux__ in specific code

Anders lösen (oder structs/pointers aufmachen):
myFile. ersetzen
sd. ersetzen
display. ersetzen
stopSnesClocks_resetCic_resetCart bearbeiten (?)
setup_Snes()	# done by kernel driver bzw. hier den snes_fd anlegen
controlOut_SNES()	# spezieller syscall oder ioctl
controlIn_SNES()
i2c_found
clockgen.

*/

// EEPROM_readAnything()?

static void display_setCursor(int x, int y) { }

static struct display {
	void (*setCursor)(int x, int y);
} display = { display_setCursor };

static void sd_chdir(const char *dir) { }
static int file_open(char *path, int flags) { return true; }
static void file_read(byte buffer, int size) { }
static void file_close() { }

typedef struct sd {
	void (*chdir)(const char *dir);
	FsFile (*open)(char *path, int flags);
	void (*write)(byte buffer, int size);
	void (*read)(byte buffer, int size);
	void (*close)(void);
} FsFile;

static FSFile sd = { sd_chdir };
static FSFile myFile= { sd_chdir, file_open, file_write, file_read, file_close };

/* wir brauchen noch
Serial.print, Serial.println - bei defined(ENABLE_SERIAL)
clockgen.output_enable(clockport, on/off)
clockgen.set_freq(freq, clockport)
*/

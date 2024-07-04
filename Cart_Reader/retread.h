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
#define wait()
#define PROGMEM
#define NOP

// no direct hardware port access (effectively disables read/write)

unsigned char dummy;
#define PORTC	dummy
#define PORTF	dummy
#define PORTG	dummy
#define PORTH	dummy
#define PORTL	dummy
#define PORTK	dummy
#define DDRA	dummy
#define DDRC	dummy
#define DDRE	dummy
#define DDRG	dummy
#define DDRH	dummy
#define DDRJ	dummy
#define DDRK	dummy
#define DDRL	dummy

/* solve this by #ifdef __Linux__ in specific code

Anders lösen (oder structs/pointers aufmachen):
myFile. ersetzen
sd. ersetzen
stopSnesClocks_resetCic_resetCart bearbeiten (?)
setup_Snes()	# done by kernel driver bzw. hier den snes_fd anlegen
controlOut_SNES()	# spezieller syscall oder ioctl
controlIn_SNES()
i2c_found
clockgen.

durch lseek, read, write ersetzen:
writeBank_SNES
readBank_SNES
readLoRomBanks
readHiRomBanks

*/
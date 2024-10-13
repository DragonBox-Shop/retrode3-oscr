/*
 * RetRead 3 HID translation daemon
 *
 * function:
 *	reads /dev/input/events from game ports
 * 	writes HID event packages for configs
 *
 * usage:
 * 	/root/hidd /dev/input/event1 /dev/hidg1&
 *
 * for testing:
 * 	./hidd /dev/input/event0 /dev/fd/1 | xxd
 *
 * to build on target device:
 * 	make hidd
 *
 * to build in Letux tree: cc -I arch/mips/include -I arch/mips/include/generated -I include/uapi -I arch/mips/include/uapi Letux/root/hidd.c
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <unistd.h>
#include <linux/input.h>	// UAPI
#include <linux/input-event-codes.h>

/*
 * should scan carts, usb events etc.
 * maybe triggered by udev on USB plugin
 * translate game pad input events to HID events
	kernel events: https://www.kernel.org/doc/html/v4.18/input/input.html#event-interface
	HID device events https://www.usb.org/hid https://www.usb.org/sites/default/files/hid1_11.pdf
 * detect carts, read out cart type and size
 * copy/map to file on µSD
 * present through configfs as a mmc
 * potentially handle writing a RAM cart
 */

// see also: https://www.kernel.org/doc/html/latest/usb/gadget_hid.html
// code example: https://randomnerdtutorials.com/raspberry-pi-zero-usb-keyboard-hid/
// code table: 
// Tutorial: https://www.kampis-elektroecke.de/mikrocontroller/avr8/at90usb1287-usb/usb-device/hid-eine-einfuehrung-in-das-protokoll/#:~:text=Mäuse%2C%20Tastaturen%20und%20Joysticks%2C%20werden,Mäuse%2C%20umgehen%20zu%20können.
// Sample code: https://docs.kernel.org/usb/gadget_hid.html -- hid_gadget_test

int send_hid(int fd, int shift, int keycode)
{
// FIXME: this is a keyboard event; a joystick event is just 4 bytes? Figure 28 on page 146 in https://usb.org/sites/default/files/documents/hut1_12v2.pdf
// or even A.13 on Page 147 -> 2 Byte
// since we do not yet know, just send keyboar events...

	unsigned char hid_data[8];
	int n;

	memset(hid_data, 0, sizeof(hid_data));

	hid_data[0] = shift?0x20:0x00;
	hid_data[2] = keycode;	// code 0x00 is release - table 12 on page 53 in https://usb.org/sites/default/files/documents/hut1_12v2.pdf

	n = write(fd, hid_data, sizeof(hid_data));

fprintf(stderr, "sent %d bytes for shift %x and code %x\n", n, hid_data[0], hid_data[2]);

	if (n < 0) {
		perror("hid write");
		return n;
	}
	if (n != sizeof(hid_data)) {
		perror("hid write");
		return n;
	}

	fsync(fd);	// push - do we really need this?
	return 0;
}

int hid(char *device, char *hid_file)
{
	int event_fd, hid_fd;

	event_fd  =open(device, O_RDONLY);
	if (event_fd < 0) {
		perror(device);
		return 1;
	}
	hid_fd = open(hid_file, O_WRONLY);
	if (hid_fd < 0) {
		perror(hid_file);
		return 1;
	}

	while(1) {
		fd_set rfd, wfd, efd;

		FD_ZERO(&rfd);
		FD_ZERO(&wfd);
		FD_ZERO(&efd);
		FD_SET(event_fd, &rfd);	// wait for input event

		if (select(event_fd+1, &rfd, &wfd, &efd, NULL) > 0) {
			struct input_event event;
			int n;

			n = read(event_fd, &event, sizeof(event));
			if (n < 0) {
				perror(device);
				return 2;	/* read error */
			}
			if (n == 0)
				return 0;	/* EOF */

fprintf(stderr, "type=%d code=%d value=%ld\n", event.type, event.code, event.value);

			// geht! type=1 code=158/143 value=0/1 (loslassen/drücken)
			// dazwischen noch type=0 code=0 value=0?
			// abhängig von Zahl der momentan gedrückten Tasten LED anschalten?
			// if(type == 1) buttons+=value?1:-1;

			if (event.type != EV_KEY)
				continue;	// ignore

			if (event.value == 0) {
				// FIXME: how can we properly handle n-key-rollover?
				send_hid(hid_fd, 0, 0);	// released all keys
			}

			// translate into USB HID keycode
			switch (event.code) {
				case KEY_A:  send_hid(hid_fd, 1, 0x04); break;	// Shift + A
				case KEY_B:  send_hid(hid_fd, 1, 0x05); break;	// Shift + B
				case KEY_C:  send_hid(hid_fd, 1, 0x06); break;	// Shift + C
				case KEY_D:  send_hid(hid_fd, 1, 0x07); break;	// Shift + D
				case KEY_L:  send_hid(hid_fd, 1, 0x0f); break;	// Shift + L
				case KEY_M:  send_hid(hid_fd, 1, 0x10); break;	// Shift + M
				case KEY_R:  send_hid(hid_fd, 1, 0x15); break;	// Shift + R
				case KEY_S:  send_hid(hid_fd, 1, 0x16); break;	// Shift + S
				case KEY_U:  send_hid(hid_fd, 1, 0x18); break;	// Shift + U
				case KEY_X:  send_hid(hid_fd, 1, 0x1b); break;	// Shift + X
				// Achtung: depends on local keyboard!
				case KEY_Y:  send_hid(hid_fd, 1, 0x1c); break;	// Shift + Y
				case KEY_Z:  send_hid(hid_fd, 1, 0x1d); break;	// Shift + Z
// event0 buttons...
				case 143:  send_hid(hid_fd, 0, 0x04); break;	// a
				case 158:  send_hid(hid_fd, 0, 0x05); break;	// b
			}
		}
	}
	return 0;
}

int main(int argc, char *argv[])
{
// FIXME: pass this as argv[] so that we can add two daemons for each game controller
	if(!argv[1] || !argv[2]) {
		fprintf(stderr, "usage: hidd /dev/input/event* /dev/hidg*&\n");
		exit(1);
	}
	return hid(argv[1], argv[2]);
}

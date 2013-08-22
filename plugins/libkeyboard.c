/*
 * PURE - Keyboard Library
 * 
 * Copyright (C) 2013 Christopher Cope
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see {http://www.gnu.org/licenses/}.
 */

#include <stdlib.h> 
#include <stdio.h>
#include <string.h>
#include <linux/types.h>
#include <linux/usb/gadgetfs.h>
#include <asm/byteorder.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>
#include <math.h>

#include "../usbstring.h"
#include "libkeyboard.h"

#define	STRINGID_MFGR		0x01
#define	STRINGID_PRODUCT	0x02
#define	STRINGID_SERIAL		0x03
#define	STRINGID_CONFIG		0x04
#define	STRINGID_INTERFACE	0x05

#define rsize 8

typedef void (*sst)(char *name, int status);
sst ret;

int iosize=rsize;
int shiftState=-1;
int fnState=0;
#define DRIVER_VENDOR_NUM 0x045e
#define DRIVER_PRODUCT_NUM 0x000b

static char serial [32];
static struct usb_string stringtab [] = {
	{ STRINGID_MFGR,	"Microsoft Corp.", },
	{ STRINGID_PRODUCT,	"Natural Keyboard Elite", },
	{ STRINGID_SERIAL,	serial, },
	{ STRINGID_CONFIG,	"The Configuration", },
	{ STRINGID_INTERFACE,	"Source/Sink", },
};

struct usb_gadget_strings strings = {
	.language =	0x0409,		/* "en-us" */
	.strings =	stringtab,
};

static int keyboard_idle_config=125;

char ReportDescriptor[] = {
	0x05, 0x01, // USAGE_PAGE (Generic Desktop)
	0x09, 0x06, // USAGE (Keyboard)
	0xa1, 0x01, // COLLETION (Application)
	0x75, 0x01, //	REPORT_SIZE (1)
	0x95, 0x08, //	REPORT_COUNT (8)
	0x05, 0x07, //	USAGE_PAGE (Key Codes)
	0x19, 0xe0, //	USAGE_MINIMUM (224)
	0x29, 0xe7, // 	USAGE_MAXIMUM (231)
	0x15, 0x00, //	LOGICAL_MINIMUM (0)
	0x25, 0x01, //	LOGICAL_MAXIMUM (1)
	0x81, 0x02, //	INPUT (Data,Var,Abs)
    0x75, 0x08, //  REPORT_SIZE (8),
    0x95, 0x01, //  REPORT_COUNT (1),
    0x81, 0x03, //  Input (Cnst), 
	0x75, 0x08, //	REPORT_SIZE (8)
	0x95, 0x06, //	REPORT_COUNT (6)
	0x15, 0x00, //	LOGICAL_MINIMUM (0)
	0x25, 0x68, //	LOGICAL_MAXIMUM (104)
	0x05, 0x07, //	USAGE_PAGE (Key Codes)
	0x19, 0x00, //	USAGE_MINIMUM (0)
	0x29, 0x68, //	USAGE_MAXIMUM (104)
	0x81, 0x00, //	INPUT(Data,Ary)
	0xc0		// END_COLLECTION
};

struct usb_device_descriptor device_desc = {
	.bLength =		sizeof device_desc,
	.bDescriptorType =	USB_DT_DEVICE,
	.bDeviceClass =		0,
	.bDeviceSubClass =	0,
	.bDeviceProtocol =	0,
	// .bMaxPacketSize0 ... set by gadgetfs
	.idVendor =		__constant_cpu_to_le16(DRIVER_VENDOR_NUM),
	.idProduct =	__constant_cpu_to_le16(DRIVER_PRODUCT_NUM),
	.iManufacturer =	STRINGID_MFGR,
	.iProduct =		STRINGID_PRODUCT,
	.iSerialNumber =	STRINGID_SERIAL,
	.bNumConfigurations =	1,
};


int reportDescSize = sizeof(ReportDescriptor);

__u8 pad[rsize] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

void reset_pad()
{
	__u8 sbuf[rsize] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	memcpy(&pad,sbuf,rsize);
}

void setSST(sst set){
	ret=set;
}

__u8 *getReport(){
	return &pad;
}

int fnMap(int code){
	if((pad[0] >> 1)%2) // Check for Shift
		shiftState=1;
	else
		shiftState=0;
	switch (code){
		case 0xBE: // @
			if(shiftState==0)
				pad[0]+=2;
			return 3;
		default:
			return code;
	}
}
		

int handleKey(int code, int action){
	// Key layout hack
	if((pad[0] >> 1)%2){ // Check for Shift
		if((code >=2 && code <= 5)) // Keys 1-4
			code = shiftMap[code-2];
		else if(code == 10 || code == 11){ // Keys 9 & 0
			pad[0]-=2;
			shiftState=1;
			code = shiftMap[code-9];
		}else if(code == 14) // BackSpace
			code = shiftMap[4];
	}
	
	if(fnState && code != 0x1D0){
		code=fnMap(code);
		if(action==KEY_RELEASE)
			fnState=0;
	}
	// Function Key Handle
	if(code == 0x1D0){ // Function key
		if(action==KEY_PRESS)
			fnState=1;
	}
	printf("Key Code: %X\n", code);
	if(code > 0x70){ // Out of keyMap bounds
		printf("Key Code: %X is not mapped\n", code);
		return 0;
	}
	printf("Mapped To: %X\n", keyMap[code]);
	if(keyMap[code] >= 0xE0){ // Modifiers
		if(action==KEY_PRESS)
			pad[0]+=pow(2,keyMap[code]-0xE0);
		else  // Release
			pad[0]-=pow(2,keyMap[code]-0xE0);
	}else{
		__u8 *keyboard_keys=&pad[2];
		int i, ind;
		if(action==KEY_PRESS){
			for(i=0;i<6;i++){
				if(keyboard_keys[i]==0)
					ind=i;
				if(keyboard_keys[i]==keyMap[code])
					return 1;
			}
			keyboard_keys[ind]=keyMap[code];
		}else{	// Release
			for(i=0;i<6;i++){
				if(keyboard_keys[i]==keyMap[code])
					keyboard_keys[i]=0;
				}
		}
	}
	return 0;
}

void input_handle(char *param, int status){
	int keyboard = 0, gpio = 0;
	int count = 0;
	int eventSize = sizeof(struct input_event);
	int bytesRead = 0, gpioRead = 0;
	struct input_event event[128];
	keyboard = open("/dev/input/by-path/platform-omap_i2c.1-platform-twl4030_keypad-event", O_RDONLY | O_NONBLOCK);
	gpio = open("/dev/input/by-path/platform-gpio-keys-event", O_RDONLY | O_NONBLOCK);
	if(keyboard == -1 || gpio == -1){
		printf("Unable to open keyboard");
		exit(1);
	}
	while(1){		
		bytesRead = read(keyboard, event, eventSize * 64);
		gpioRead = read(gpio, &event[64], eventSize * 64);
			
		for (count=0;count<(bytesRead/eventSize);count++){
			if(EV_KEY == event[count].type){
				if(event[count].value == KEY_PRESS){
					handleKey(event[count].code,KEY_PRESS);
				}else if(event[count].value == KEY_RELEASE){
					handleKey(event[count].code,KEY_RELEASE);
				}
			}
		}
		
		// Handle GPIO keys
		for (count=0;count<(gpioRead/eventSize);count++){
			if(EV_KEY == event[count+64].type){
				if(event[count+64].value == KEY_PRESS){
					handleKey(event[count+64].code,KEY_PRESS);
				}else if(event[count+64].value == KEY_RELEASE){
					handleKey(event[count+64].code,KEY_RELEASE);
				}
			}
		}
		(*ret)(param,status);
		sleep(keyboard_idle_config/1000);
		
		// Key layout hack
		if(shiftState==1){
			pad[0]+=2;
			shiftState=-1;
		}else if(shiftState==0){
			pad[0]-=2;
			shiftState=-1;
		}
	}
}

int init(){
	int i,c;
	/* random initial serial number */
	srand ((int) time (0));
	for (i = 0; i < sizeof serial - 1; ) {
		c = rand () % 127;
		if ((('a' <= c && c <= 'z') || ('0' <= c && c <= '9')))
			serial [i++] = c;
	}
	reset_pad();
	return 0;
}

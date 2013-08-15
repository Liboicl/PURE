#include <stdio.h>
#include <string.h>
#include <linux/types.h>
#include <SDL/SDL.h>
#include <linux/usb/gadgetfs.h>
#include <asm/byteorder.h>
#include <time.h>

#include "../pandorakeys.h"
#include "../usbstring.h"

#define	STRINGID_MFGR		0x01
#define	STRINGID_PRODUCT	0x02
#define	STRINGID_SERIAL		0x03
#define	STRINGID_CONFIG		0x04
#define	STRINGID_INTERFACE	0x05

#define rsize 19

SDL_Event keyevent;

typedef void (*sst)(char *name, int status);
sst ret;

int iosize=rsize;
#define DRIVER_VENDOR_NUM 0x10C4
#define DRIVER_PRODUCT_NUM 0x82C0

static char serial [32];
static struct usb_string stringtab [] = {
	{ STRINGID_MFGR,	"SEGA", },
	{ STRINGID_PRODUCT,	"VIRTUA STICK High Grade", },
	{ STRINGID_SERIAL,	serial, },
	{ STRINGID_CONFIG,	"The Configuration", },
	{ STRINGID_INTERFACE,	"Source/Sink", },
};

struct usb_gadget_strings strings = {
	.language =	0x0409,		/* "en-us" */
	.strings =	stringtab,
};

typedef struct buttons {
	int left;
	int right;
	int up;
	int down;
	int cross;
	int circle;
	int triangle;
	int square;
	int L2;
	int R2;
	int select;
	int start;
	int ps;
} buttons;

buttons state = {
	.left=0,
	.right=0,
	.up=0,
	.down=0,
	.cross=0,
	.circle=0,
	.triangle=0,
	.square=0,
	.L2=0,
	.R2=0,
	.select=0,
	.start=0,
	.ps=0
};

char ReportDescriptor[] = {
	 0x05, 0x01, // USAGE_PAGE (Generic Desktop)
	 0x09, 0x05, // USAGE (Gamepad)
	 0xa1, 0x01, // COLLECTION (Application)
	 0x15, 0x00, // LOGICAL_MINIMUM (0)
	 0x25, 0x01, // LOGICAL_MAXIMUM (1)
	 0x35, 0x00, // PHYSICAL_MINIMUM (0)
	 0x45, 0x01, // PHYSICAL_MAXIMUM (1)
	 0x75, 0x01, // REPORT_SIZE (1)
	 0x95, 0x0d, // REPORT_COUNT (13)
	 0x05, 0x09, // USAGE_PAGE (Button)
	 0x19, 0x01, // USAGE_MINIMUM (Button 1)
	 0x29, 0x0d, // USAGE_MAXIMUM (Button 13)
	 0x81, 0x02, // INPUT (Data,Var,Abs)
	 0x95, 0x03, // REPORT_COUNT (3)
	 0x81, 0x01, // INPUT (Cnst,Ary,Abs)
	 0x05, 0x01, // USAGE_PAGE (Generic Desktop)
	 0x25, 0x07, // LOGICAL_MAXIMUM (7)
	 0x46, 0x3b, 0x01, // PHYSICAL_MAXIMUM (315)
	 0x75, 0x04, // REPORT_SIZE (4)
	 0x95, 0x01, // REPORT_COUNT (1)
	 0x65, 0x14, // UNIT (Eng Rot:Angular Pos)
	 0x09, 0x39, // USAGE (Hat switch)
	 0x81, 0x42, // INPUT (Data,Var,Abs,Null)
	 0x65, 0x00, // UNIT (None)
	 0x95, 0x01, // REPORT_COUNT (1)
	 0x81, 0x01, // INPUT (Cnst,Ary,Abs)
	 0x26, 0xff, 0x00, // LOGICAL_MAXIMUM (255)
	 0x46, 0xff, 0x00, // PHYSICAL_MAXIMUM (255)
	 0x09, 0x30, // USAGE (X)
	 0x09, 0x31, // USAGE (Y)
	 0x09, 0x32, // USAGE (Z)
	 0x09, 0x35, // USAGE (Rz)
	 0x75, 0x08, // REPORT_SIZE (8)
	 0x95, 0x04, // REPORT_COUNT (4)
	 0x81, 0x02, // INPUT (Data,Var,Abs)
	 0x06, 0x00, 0xff, // USAGE_PAGE (Vendor Specific)
	 0x09, 0x20, // Unknown
	 0x09, 0x21, // Unknown
	 0x09, 0x22, // Unknown
	 0x09, 0x23, // Unknown
	 0x09, 0x24, // Unknown
	 0x09, 0x25, // Unknown
	 0x09, 0x26, // Unknown
	 0x09, 0x27, // Unknown
	 0x09, 0x28, // Unknown
	 0x09, 0x29, // Unknown
	 0x09, 0x2a, // Unknown
	 0x09, 0x2b, // Unknown
	 0x95, 0x0c, // REPORT_COUNT (12)
	 0x81, 0x02, // INPUT (Data,Var,Abs)
	 0x0a, 0x21, 0x26, // Unknown
	 0x95, 0x08, // REPORT_COUNT (8)
	 0xb1, 0x02, // FEATURE (Data,Var,Abs)
	 0xc0 // END_COLLECTION
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

__u8 pad[rsize] = { 0x00, 0x00, 0x08, 0x80, 0x80, 0x80, 0x80, 0x00,
						   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
						   0x00, 0x00, 0x00 };

void reset_pad()
{
	__u8 sbuf[rsize] = { 0x00, 0x00, 0x08, 0x80, 0x80, 0x80, 0x80, 0x00,
					     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					     0x00, 0x00, 0x00 };
	memcpy(&pad,sbuf,rsize);
}

void setSST(sst set){
	ret=set;
}

__u8 *getReport(){
	return &pad;
}

void calcPad(char *param, int status, int mode){
	int dir=0x08, tmp[2]= {0,0};
	switch(mode){
		case 0: // D-Pad
			if(state.left){
				dir=0x06;
				if(state.up)
					dir+=1;
				else if(state.down)
					dir-=1;
			}else if(state.right){
				dir=0x02;
				if(state.up)
					dir-=1;
				else if(state.down)
					dir+=1;
			}else if(state.up){
				dir=0x00;
			}else if(state.down){
				dir=0x04;
			}
			pad[2]=dir;
			break;
		case 1: // Buttons
			if(state.L2)
				tmp[0]+=0x40; // Button 6
			if(state.R2)
				tmp[0]+=0x80; // Button 7
			// Had to swap A<->Y & X<->B
			if(state.triangle)
				tmp[0]+=0x08; // Button 3  *was button 0*
			if(state.cross)
				tmp[0]+=0x02; // Button 1 *was button 2*
			if(state.circle)
				tmp[0]+=0x04; // Button 2 *was button 1*
			if(state.square)
				tmp[0]+=0x01; // Button 0 *was button 3*
			if(state.select)
				tmp[1]+=0x01; // Button 8
			if(state.start)
				tmp[1]+=0x02; // Button 9
			if(state.ps)
				tmp[1]+=0x10; // Button 12
			pad[0]=tmp[0];
			pad[1]=tmp[1];
			break;
		default:
			break;
	}
	(*ret)(param,status);
}

void input_handle(char *param, int status){
	if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) != 0){
		printf("%s", SDL_GetError());
		exit(-1);
	}
	
	if( !SDL_SetVideoMode( 320,200,0,0)){
		fprintf(stderr, "Could not set video mode: %s\n", SDL_GetError());
		exit(-1);
	}
	
	// Joystick Setup
	int num_of_joy=SDL_NumJoysticks(), i;
	
	SDL_Joystick *nub0, *nub1;
	SDL_JoystickEventState(SDL_ENABLE);
	
	if (num_of_joy>0){
		for(i=0; i<num_of_joy; i++){
			if(strncmp(SDL_JoystickName(i),"nub0",4)==0)
				nub0=SDL_JoystickOpen(i);
			else if(strncmp(SDL_JoystickName(i),"nub1",4)==0)
				nub1=SDL_JoystickOpen(i);
		}
	}
	
	SDL_EnableUNICODE(1);
	int quit=0,x=0x80,y=0x80;
		
	while(!quit){
		while(SDL_PollEvent(&keyevent)){
			switch(keyevent.type){
				case SDL_KEYDOWN:
					switch(keyevent.key.keysym.sym){
						case PAN_L: // Button 6
							state.L2=1;
							calcPad((char*)param,status,1);
							break;
						case PAN_R: // Button 7
							state.R2=1;
							calcPad((char*)param,status,1);
							break;
						case SDLK_LEFT:
							state.left=1;
							calcPad((char*)param,status,0);
							break;
						case SDLK_RIGHT:
							state.right=1;
							calcPad((char*)param,status,0);
							break;
						case SDLK_UP:
							state.up=1;
							calcPad((char*)param,status,0);
							break;
						case SDLK_DOWN:
							state.down=1;
							calcPad((char*)param,status,0);
							break;
						// Had to swap A<->Y & X<->B
						case PAN_Y: // Button 3
							state.triangle=1;
							calcPad((char*)param,status,1);
							break;
						case PAN_X: // Button 1
							state.cross=1;
							calcPad((char*)param,status,1);
							break;
						case PAN_B:	// Button 2
							state.circle=1;
							calcPad((char*)param,status,1);
							break;
						case PAN_A:	// Button 0
							state.square=1;
							calcPad((char*)param,status,1);
							break;
						case PAN_SELECT: // Button 8
							state.select=1;
							calcPad((char*)param,status,1);
							break;
						case PAN_START: // Button 9
							state.start=1;
							calcPad((char*)param,status,1);
							break;
						case PAN_BUT: // Button 12
							state.ps=1;
							calcPad((char*)param,status,1);
							break;
						default:
							printf("Unknown Key: %i\n", keyevent.key.keysym.sym);
					}
					break;
				case SDL_KEYUP:
					switch(keyevent.key.keysym.sym){
						case PAN_L: // Button 6
							state.L2=0;
							calcPad((char*)param,status,1);
							break;
						case PAN_R: // Button 7
							state.R2=0;
							calcPad((char*)param,status,1);
							break;
						case SDLK_LEFT:
							state.left=0;
							calcPad((char*)param,status,0);
							break;
						case SDLK_RIGHT:
							state.right=0;
							calcPad((char*)param,status,0);
							break;
						case SDLK_UP:
							state.up=0;
							calcPad((char*)param,status,0);
							break;
						case SDLK_DOWN:
							state.down=0;
							calcPad((char*)param,status,0);
							break;
						// Had to swap A<->Y & X<->B
						case PAN_Y: // Button 3
							state.triangle=0;
							calcPad((char*)param,status,1);
							break;
						case PAN_X: // Button 1
							state.cross=0;
							calcPad((char*)param,status,1);
							break;
						case PAN_B:	// Button 2
							state.circle=0;
							calcPad((char*)param,status,1);
							break;
						case PAN_A:	// Button 0
							state.square=0;
							calcPad((char*)param,status,1);
							break;
						case PAN_SELECT: // Button 8
							state.select=0;
							calcPad((char*)param,status,1);
							break;
						case PAN_START: // Button 9
							state.start=0;
							calcPad((char*)param,status,1);
							break;
						case PAN_BUT: // Button 12
							state.ps=0;
							calcPad((char*)param,status,1);
							break;
						default:
							printf("Unknown KeyUp: %i\n", keyevent.key.keysym.sym);
						}
					break;
				case SDL_JOYAXISMOTION: // -33000 <-> 33000  scaled to 0 <-> 255
					if((keyevent.jaxis.value < -3200)||(keyevent.jaxis.value > 3200)){
						if(keyevent.jaxis.axis==0){
							x=(keyevent.jaxis.value+33000)*(255./66000);
							printf("X axis: %i\n",x);
						}
						if(keyevent.jaxis.axis==1){
							y=(keyevent.jaxis.value+33000)*(255./66000);
							printf("Y axis: %i\n",y);
						}
					}else{
						if(keyevent.jaxis.axis==0)
							x=0x80;
						else if(keyevent.jaxis.axis==1)
							y=0x80;
					}
					if(keyevent.jaxis.which==0){
						pad[3]=x;
						pad[4]=y;
					}else{
						pad[5]=x;
						pad[6]=y;
					}
					(*ret)(param,status);
					break;
				case SDL_QUIT:
					quit=1;
			}
		}
	}
	SDL_Quit();
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

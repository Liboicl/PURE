/*
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
 
#include <linux/types.h>
#include <linux/usb/ch9.h>

typedef int (*Init)();
typedef void (*sst)(char *name, int status);
typedef void (*SetSST)(sst set);
typedef void (*Input_handle)(char *param, int status);
typedef __u8 *(*GetReport)();
// Function Types

typedef struct{
	void *so;
	/* Functions */
	Init init;
	SetSST setSST;
	Input_handle input_handle;
	GetReport getReport;
	/* Info */
	int *iosize;
	/* Descriptors */
	char *ReportDescriptor;
	struct usb_device_descriptor *device_desc;
	struct usb_gadget_strings *strings;
	int *reportDescSize;
} Plugin;
	
	
void loadPlugin(Plugin *plugin, const char *soname);
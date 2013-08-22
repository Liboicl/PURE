/*
 * Basic Command-line Interface
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
#include <unistd.h>

#include "libPURE.h"

int main(int argc, char *argv[]){
	int fd;
	char feed[80];
	FILE *config;
	
	config = fopen("PURE.cfg", "rt");
	if(config != NULL){
		if(fscanf(config, "PLUGIN_DIR=%s", feed) == 1){
			printf("Configuration loaded successfully.\n");
			setPluginDir(feed);
		}
	}else{
		printf("Failed to find configuration file; default values set.\n");
		setPluginDir("./plugins/");
	}
	if (chdir ("/dev/gadget") < 0) {
		perror ("can't chdir /dev/gadget");
		return 1;
	}
	
	if(argc != 2){
		perror("Please specify a plugin to load.\n");
		exit(1);
	}
	
	fd=init_device(argv[1]);
	if (fd < 0)
		return 1;

	fprintf (stderr, "/dev/gadget/%s ep0 configured\n",
		"musb-hdrc");
	fflush (stderr);
	(void) ep0_thread (&fd);
	return 0;
}
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
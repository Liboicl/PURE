#include <stdio.h>
#include <stdlib.h> 
#include <dlfcn.h>
#include "plugin.h"

int dltest(const char *msg)
{
    char *error;
    if((error = dlerror())){
		printf("%s: %s\n", msg, error);
		return 1;
    }
    return 0;
}

void loadPlugin(Plugin *plugin, const char *soname){
	printf("Loading %s...\n", soname);
	
	plugin->so = dlopen(soname, RTLD_LAZY);
	if(dltest("Failed to load plugin.")) exit(1);
	
	if(plugin->so){
	// Load Info
		plugin->iosize = dlsym(plugin->so, "iosize");
		if(dltest("Plugin is missing iosize.")) exit(1);
		plugin->ReportDescriptor = dlsym(plugin->so, "ReportDescriptor");
		if(dltest("Plugin is missing a Report Descriptor.")) exit(1);
		plugin->reportDescSize = dlsym(plugin->so, "reportDescSize");
		if(dltest("Plugin is missing Report Descriptor Size.")) exit(1);
		plugin->device_desc = dlsym(plugin->so, "device_desc");
		if(dltest("Plugin is missing Device Descriptor.")) exit(1);
		plugin->strings = dlsym(plugin->so, "strings");
		if(dltest("Plugin is missing String Descriptors.")) exit(1);
		
	// Load Functions
		plugin->init = dlsym(plugin->so, "init");
		if(dltest("Plugin is missing initialize function.")) exit(1);
		plugin->setSST = dlsym(plugin->so, "setSST");
		if(dltest("Plugin is missing setSST function.")) exit(1);
		plugin->input_handle = dlsym(plugin->so, "input_handle");
		if(dltest("Plugin is missing input_handle function.")) exit(1);
		plugin->getReport = dlsym(plugin->so, "getReport");
		if(dltest("Plugin is missing getReport function.")) exit(1);
		/*plugin->test = dlsym(plugin->so, "test");
		if(dltest("Plugin is missing test function.")) exit(1);*/
	}
}

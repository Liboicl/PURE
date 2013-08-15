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
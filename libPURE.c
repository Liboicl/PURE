#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <memory.h>
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <dlfcn.h>

#include <asm/byteorder.h>

#include <linux/usb/gadgetfs.h>

#include "plugin.h"
#include "usbstring.h"

#define	STRINGID_MFGR		0x01
#define	STRINGID_PRODUCT	0x02
#define	STRINGID_SERIAL		0x03
#define	STRINGID_CONFIG		0x04
#define	STRINGID_INTERFACE	0x05

#define	USB_BUFSIZE	(7 * 1024)

static int verbose=0;
static enum usb_device_speed	current_speed;

char *pluginDir;

int setPluginDir(char *dir){
	char cwd[1024];
	if(*dir == '.'){
		char *tmp;
		printf("Relative path found.\n");
		if(getcwd(cwd, sizeof(cwd)) == NULL){
			perror("Error reading current directory.\n"); 
			exit(1);
		}
		tmp=malloc(strlen(dir)+strlen(cwd)+1);
		sprintf(tmp, "%s%s", cwd, ++dir);
		printf("Directory: %s\n", tmp);
		pluginDir=malloc(strlen(tmp)+1);
		memcpy(pluginDir, tmp, strlen(tmp)+1);
		free(tmp);
	}else{
		pluginDir=malloc(strlen(dir)+1);
		memcpy(pluginDir, dir, sizeof(dir));
	}
	return 0;
}

#define	CONFIG_VALUE		1

const char OperationMode[] = {
	0x21, 0x26,
	0x01, 0x07,
	0x00, 0x00,
	0x00, 0x00
};

const unsigned int OperationModeSize = sizeof(OperationMode);

/* The following needs to be loaded from each plugin */
// Todo: Allow Multiple Plugins At Once
Plugin plugin;

static const struct usb_config_descriptor
config = {
	.bLength =		sizeof config,
	.bDescriptorType =	USB_DT_CONFIG,

	/* must compute wTotalLength ... */
	.bNumInterfaces =	1,
	.bConfigurationValue =	1,
	.iConfiguration = 	STRINGID_CONFIG,
	.bmAttributes =		0x80,
	.bMaxPower =		0xFA //(MAX_USB_POWER + 1) / 2,
};

/* Temporary Unknown Functions */
static struct usb_interface_descriptor
source_sink_intf = {
	.bLength =		sizeof source_sink_intf,
	.bDescriptorType =	USB_DT_INTERFACE,

	.bInterfaceClass =	USB_CLASS_HID,
	.bInterfaceSubClass =	0x00,
	.bInterfaceProtocol =	0x00,
	.iInterface =		STRINGID_INTERFACE,
};

static struct usb_endpoint_descriptor
fs_status_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_INT,
	//.wMaxPacketSize =	__constant_cpu_to_le16 (iosize),
	.bInterval =		10,
};

static const struct usb_endpoint_descriptor *fs_eps [] = {
	&fs_status_desc,
};

static struct usb_endpoint_descriptor
hs_status_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_INT,
	//.wMaxPacketSize =	__constant_cpu_to_le16 (iosize),//__constant_cpu_to_le16 (STATUS_MAXPACKET),
	.bInterval =		5,
};

static const struct usb_endpoint_descriptor *hs_eps [] = {
	&hs_status_desc,
};

struct hid_class_descriptor {
	__u8 bDescriptorType;
	__le16 wDescriptorLength;
} __attribute__ ((packed));

struct hid_descriptor {
	__u8 bLength;
	__u8 bDescriptorType;
	__le16 bcdHID;
	__u8 bCountryCode;
	__u8 bNumDescriptors;

	struct hid_class_descriptor desc[1];
} __attribute__ ((packed));



static struct hid_descriptor
hid_desc = {
	.bLength = 9,
	.bDescriptorType = USB_DT_CS_DEVICE,
	.bcdHID = __constant_cpu_to_le16(0x0110),
	.bCountryCode = 0,
	.bNumDescriptors = 1,
};
/*                         */

static void signothing (int sig, siginfo_t *info, void *ptr)
{
	/* NOP */
	if (verbose > 2)
		fprintf (stderr, "%s %d\n", __FUNCTION__, sig);
}

static const char *speed (enum usb_device_speed s)
{
	switch (s) {
	case USB_SPEED_LOW:	return "low speed";
	case USB_SPEED_FULL:	return "full speed";
	case USB_SPEED_HIGH:	return "high speed";
	default: 		return "UNKNOWN speed";
	}
}

static pthread_t	ep0;

static pthread_t	source;
static int		source_fd = -1;

// FIXME no status i/o yet

static void close_fd (void *fd_ptr)
{
	int	status, fd;

	fd = *(int *)fd_ptr;
	*(int *)fd_ptr = -1;

	/* test the FIFO ioctls (non-ep0 code paths) */
	if (pthread_self () != ep0) {
		status = ioctl (fd, GADGETFS_FIFO_STATUS);
		if (status < 0) {
			/* ENODEV reported after disconnect */
			if (errno != ENODEV && errno != -EOPNOTSUPP)
				perror ("get fifo status");
		} else {
			if(verbose) fprintf (stderr, "fd %d, unclaimed = %d\n", fd, status);
			if (status) {
				status = ioctl (fd, GADGETFS_FIFO_FLUSH);
				if (status < 0)
					perror ("fifo flush");
			}
		}
	}

	if (close (fd) < 0)
		perror ("close");
}

static int	HIGHSPEED;
static char	*DEVNAME;
static char	*EP_STATUS_NAME;

static char *build_config (char *cp, const struct usb_endpoint_descriptor **ep)
{
	struct usb_config_descriptor *c;
	int i;

	c = (struct usb_config_descriptor *) cp;

	memcpy (cp, &config, config.bLength);
	cp += config.bLength;
	memcpy (cp, &source_sink_intf, source_sink_intf.bLength);
	cp += source_sink_intf.bLength;
	memcpy (cp, &hid_desc, hid_desc.bLength);
	cp += hid_desc.bLength;

	for (i = 0; i < source_sink_intf.bNumEndpoints; i++) {
		memcpy (cp, ep [i], USB_DT_ENDPOINT_SIZE);
		cp += USB_DT_ENDPOINT_SIZE;
	}
	c->wTotalLength = __cpu_to_le16 (cp - (char *) c);
	return cp;
}

static int
ep_config (char *name, const char *label,
        struct usb_endpoint_descriptor *fs,
        struct usb_endpoint_descriptor *hs
)
{
        int             fd, status;
        char            buf [USB_BUFSIZE];

        /* open and initialize with endpoint descriptor(s) */
        fd = open (name, O_RDWR);
        if (fd < 0) {
                status = -errno;
                if(verbose) fprintf (stderr, "%s open %s error %d (%s)\n", label, name, errno, strerror (errno));
                return status;
        }

        /* one (fs or ls) or two (fs + hs) sets of config descriptors */
        *(__u32 *)buf = 1;      /* tag for this format */
        memcpy (buf + 4, fs, USB_DT_ENDPOINT_SIZE);
        if (HIGHSPEED)
                memcpy (buf + 4 + USB_DT_ENDPOINT_SIZE,
                        hs, USB_DT_ENDPOINT_SIZE);
        status = write (fd, buf, 4 + USB_DT_ENDPOINT_SIZE
                        + (HIGHSPEED ? USB_DT_ENDPOINT_SIZE : 0));
        if (status < 0) {
                status = -errno;
                if(verbose) fprintf (stderr, "%s config %s error %d (%s)\n",label, name, errno, strerror (errno));
                close (fd);
                return status;
        } else if (verbose) {
                unsigned long   id;

                id = pthread_self ();
                if(verbose) fprintf (stderr, "%s start %ld fd %d\n", label, id, fd);
        }
        return fd;
}

#define status_open(name) \
        ep_config(name,__FUNCTION__, &fs_status_desc, &hs_status_desc)

static void *simple_source_thread (char *name, int status)
{
	char		buf [USB_BUFSIZE];

	if(verbose) printf("%s sending event\n", name);

	memcpy(buf,(*plugin.getReport)(),*plugin.iosize);
	status = write (source_fd, buf, *plugin.iosize);
	if(verbose) printf("%s sent event %d %m\n", name, status);

	if (status >= 0) {
		if (verbose) fprintf (stderr, "done %s\n", __FUNCTION__);
	} else if (verbose > 2 || errno != ESHUTDOWN) /* normal disconnect */
		perror ("write");

	return 0;
}

static void *input_handle(void *param){
	int status;
		
	status = status_open ((char *)param);
	if (status < 0)
		return 0;

	source_fd = status;
	
	pthread_cleanup_push (close_fd, &source_fd);
	
	sst func = simple_source_thread;
	(*plugin.setSST)(func);
	(*plugin.input_handle)((char*)param,status);
	
	fflush (stdout);
	fflush (stderr);
	pthread_cleanup_pop (1);
	exit(0);
	return 0;
}

static void start_io ()
{
	sigset_t	allsig, oldsig;

	sigfillset (&allsig);
	errno = pthread_sigmask (SIG_SETMASK, &allsig, &oldsig);
	if (errno < 0) {
		perror ("set thread signal mask");
		return;
	}

	/* give the other threads a chance to run before we report
	 * success to the host.
	 * FIXME better yet, use pthread_cond_timedwait() and
	 * synchronize on ep config success.
	 */
	
        if (pthread_create (&source, 0,
                        input_handle, (void *) EP_STATUS_NAME) != 0) {
                perror ("can't create input thread");
                goto cleanup;
        }


	sched_yield ();

cleanup:
	//printf("start of start_io cleanup\n");
	errno = pthread_sigmask (SIG_SETMASK, &oldsig, 0);
	if (errno != 0) {
		perror ("restore sigmask");
		exit (-1);
	}
	if(verbose) printf("exiting start_io cleanup\n");
}

static void stop_io ()
{
	if (!pthread_equal (source, ep0)) {
		pthread_cancel (source);
		if (pthread_join (source, 0) != 0)
			perror ("can't join source thread");
		source = ep0;
	}
}

static void handle_control (int fd, struct usb_ctrlrequest *setup, int s_iCounter)
{
	int		status, tmp;
	__u8		buf [256];

	__u16		value, index, length;

	value = __le16_to_cpu(setup->wValue);
	index = __le16_to_cpu(setup->wIndex);
	length = __le16_to_cpu(setup->wLength);

	if(verbose)
		fprintf (stderr, "s_iCounter = %x\n SETUP %02x.%02x "
				"v%04x i%04x %d\n",
			s_iCounter, setup->bRequestType, setup->bRequest,
			value, index, length);

	if(verbose) printf("fd in handle_control = %x\n", fd);
	//fd is the same each time this function is called

	switch (setup->bRequest) {	/* usb 2.0 spec ch9 requests */
	case 1:
		if(verbose) printf("operation mode");
		memcpy(buf, OperationMode, OperationModeSize);
		status=write(fd, buf, OperationModeSize);
		if(verbose) printf("status for write operation mode = %x\n", status);
		if (status < 0)
		{
			if (errno == EIDRM)
				printf ("GET_REPORT timeout\n");
			else if(verbose)
				perror ("write GET_REPORT data");
		}
		return;
	case USB_REQ_GET_DESCRIPTOR:
		if(verbose) printf("In handle_control Get Descriptor case\n");

		switch (value >> 8) {
		case USB_DT_STRING:
			if(verbose) printf("get descriptor string type\n");
			if (setup->bRequestType != USB_DIR_IN)
				goto stall;
			tmp = value & 0x0ff;
			if (verbose > 1)
				fprintf (stderr,
					"... get string %d lang %04x\n",
					tmp, index);
			if (tmp != 0 && index != (*plugin.strings).language)
				goto stall;
			status = usb_gadget_get_string (plugin.strings, tmp, buf);
			if (status < 0)
				goto stall;
			tmp = status;
			if (length < tmp)
				tmp = length;
			status = write (fd, buf, tmp);
			if (status < 0) {
				if (errno == EIDRM)
					fprintf (stderr, "string timeout\n");
				else if(verbose)
					perror ("write string data");
			} else if (status != tmp) 
				fprintf (stderr, "short string write, %d\n",
				status);
			break;
		case 0x22:
			if(verbose) printf("Got an report descriptor request\n");
			tmp = value & 0x00ff;
			if (verbose > 1)
				printf("... get rpt desc %d, index %04x\n", tmp, setup->wIndex);
			if (tmp != 0 && setup->wIndex != 0) 
				goto stall;
			memcpy(buf, plugin.ReportDescriptor, *plugin.reportDescSize);
			status=write(fd, buf, *plugin.reportDescSize);
			if(verbose) printf("status for write report descriptor = %x\n", status);
			if (status < 0)
			{
				if (errno == EIDRM)
					printf ("GET_REPORT timeout\n");
				else if(verbose)
					perror ("write GET_REPORT data");
			}
			break;
		default:
			goto stall;
		}
		return;

	case USB_REQ_SET_CONFIGURATION:
		if(verbose) printf("Set Configuration case\n");
		if (setup->bRequestType != USB_DIR_OUT)
			goto stall;
		if (verbose)
			fprintf (stderr, "CONFIG #%d\n", value);

		/* Kernel is normally waiting for us to finish reconfiguring
		 * the device.
		 *
		 * Some hardware can't, notably older PXA2xx hardware.(With
		 * racey and restrictive config change automagic.)To handle
		 * such hardware, don't write code this way ... instead, keep
		 * the endpoints always active and don't rely on seeing any
		 * config change events, either this or SET_INTERFACE.
		 */
		switch (value) {
		case CONFIG_VALUE:
			if(verbose) printf("about to call start_io function\n");
			start_io ();
			break;
		case 0:
			//printf("about to call stop_io function\n");
			stop_io ();
			//printf("just finished stop_io function\n");
			break;
		default:
			/* kernel bug -- "can't happen" */
			fprintf (stderr, "? illegal config\n");
			goto stall;
		}

		/* ... ack (a write would stall) */
		status = read (fd, &status, 0);
		if(verbose) printf("status of read = %x\n", status);
		if (status)
			perror ("ack SET_CONFIGURATION");
		//printf("About to return from set configuration\n");
		return;
	case USB_REQ_GET_INTERFACE:
		if(verbose) printf("Get Interface case\n");
		if (setup->bRequestType != (USB_DIR_IN|USB_RECIP_INTERFACE)
				|| index != 0
				|| length > 1)
			goto stall;

		/* only one altsetting in this driver */
		buf [0] = 0;
		status = write (fd, buf, length);
		if (status < 0) {
			if (errno == EIDRM)
				fprintf (stderr, "GET_INTERFACE timeout\n");
			else if(verbose)
				perror ("write GET_INTERFACE data");
		} else if (status != length) {
			fprintf (stderr, "short GET_INTERFACE write, %d\n",
				status);
		}
		return;
	case USB_REQ_SET_INTERFACE:
		if(verbose) printf("Set Interface case\n");
		if (setup->bRequestType != USB_RECIP_INTERFACE
				|| index != 0
				|| value != 0)
			goto stall;

		/* just reset toggle/halt for the interface's endpoints */
		status = 0;
		if (ioctl (source_fd, GADGETFS_CLEAR_HALT) < 0) {
			status = errno;
			perror ("reset source fd");
		}
		/* FIXME eventually reset the status endpoint too */
		if (status)
			goto stall;

		/* ... and ack (a write would stall) */
		status = read (fd, &status, 0);
		if (status)
			perror ("ack SET_INTERFACE");
		return;
	default:
		goto stall;
	}

stall:
	if (verbose)
		printf ("... protocol stall %02x.%02x\n",
			setup->bRequestType, setup->bRequest);

	/* non-iso endpoints are stalled by issuing an i/o request
	 * in the "wrong" direction.ep0 is special only because
	 * the direction isn't fixed.
	 */
	if (setup->bRequestType & USB_DIR_IN)
		status = read (fd, &status, 0);
	else
		status = write (fd, &status, 0);
	if (status != -1)
		printf ("can't stall ep0 for %02x.%02x\n",
			setup->bRequestType, setup->bRequest);
	else if (errno != EL2HLT)
		perror ("ep0 stall");

}

static int init_config(){
	struct stat	statb;

	/* Intel PXA 2xx processor, full speed only */
	if (stat (DEVNAME = "musb-hdrc", &statb) == 0){
		HIGHSPEED = 1;
		(*plugin.device_desc).bcdDevice = __constant_cpu_to_le16 (0x0107);

		EP_STATUS_NAME = "ep1in";

		source_sink_intf.bNumEndpoints = 1;
		hs_status_desc.bEndpointAddress =
		fs_status_desc.bEndpointAddress = USB_DIR_IN | 1;
	}
	else{
		DEVNAME = 0;
		return -ENODEV;
	}
	return 0;
}

int handlePlugins(char *name){
	char *soname=malloc(strlen(pluginDir) + strlen(name) + 1);
	sprintf(soname, "%s%s", pluginDir, name);
	// Load Plugin
	loadPlugin(&plugin, soname);
	(*plugin.init)();
	
	free(soname);
	free(pluginDir);
	
	// Set values dependant on plugin	
	hid_desc.desc->bDescriptorType = 0x22;
	hid_desc.desc->wDescriptorLength = *plugin.reportDescSize;
	
	fs_status_desc.wMaxPacketSize =	__constant_cpu_to_le16 (*plugin.iosize);
	hs_status_desc.wMaxPacketSize =	__constant_cpu_to_le16 (*plugin.iosize);
	
	return 0;
}

int init_device(char *name){
	handlePlugins(name);
	char buf [4096], *cp = &buf[0];
	int	fd;
	int	status;

	status = init_config();
	if (status < 0){
		fprintf(stderr, "?? don't recognize device: %m\n");
		return status;
	}

	fd = open(DEVNAME, O_RDWR);
	if (fd < 0){
		perror(DEVNAME);
		return -errno;
	}

	*(__u32 *)cp = 0;	/* tag for this format */
	cp += 4;

	/* write full then high speed configs */
	cp = build_config(cp, fs_eps);
	if(HIGHSPEED)
		cp = build_config(cp, hs_eps);

	/* and device descriptor at the end */
	memcpy(cp, plugin.device_desc, sizeof *plugin.device_desc);
	cp += sizeof *plugin.device_desc;

	status = write(fd, &buf [0], cp - &buf [0]);
	if(verbose) printf("status for init_device = %x\n", status);
	if (status < 0){
		perror("write dev descriptors");
		close(fd);
		return status;
	}else if(status != (cp - buf)){
		fprintf(stderr, "dev init, wrote %d expected %d\n", status, cp - buf);
		close(fd);
		return -EIO;
	}
	return fd;
}

void *ep0_thread(void *param){
	int	fd = *(int*) param;
	struct sigaction action, old;
	sigset_t sigs;
	static int s_iCounter = 0;

	source = ep0 = pthread_self ();
	pthread_cleanup_push (close_fd, param);

	/* SIGIO tells file owner that a device event is available
	 * use F_SETSIG to use a different signal.
	 * (not SIGUSR1 or SIGUSR2; linux pthreads may use them.)
	 * right now, only ep0 supports O_ASYNC signaling.
	 */
	if(fcntl(fd, F_SETOWN, getpid ()) < 0){
		perror ("set ep0 owner");
		return 0;
	}
	if(fcntl(fd, F_SETFL, O_ASYNC) < 0){
		perror ("set ep0 async mode");
		return 0;
	}
	action.sa_sigaction = signothing;
	sigfillset(&action.sa_mask);
	action.sa_flags = SA_SIGINFO;
	if(sigaction(SIGIO, &action, &old) < 0){
		perror ("sigaction");
		return 0;
	}
	/* event loop */
	sigfillset(&sigs);
	
	for(;;){
		int	tmp;
		struct usb_gadgetfs_event	event [5];
		int	connected = 0;
		int	i, nevent;

		sigwait(&sigs, &tmp);

		/* eventually any endpoint could send signals */
		switch(tmp){
		case SIGIO:		/* used only by ep0 */
			if(verbose) printf("EP0 thread SIGIO case\n");
			tmp = read(fd, &event, sizeof event);
			if(tmp < 0){
				if(verbose) printf("tmp < 0\n");
				if (errno == EAGAIN) {
					sleep (1);
					continue;
				}
				perror ("ep0 read after sigio");
				goto done;
			}
			nevent = tmp / sizeof event [0];
			if(verbose)
				fprintf(stderr, "read %d ep0 events\n", nevent);
			for(i = 0; i < nevent; i++){
				switch(event [i].type){
				case GADGETFS_NOP:
					//printf("NOP\n");
					if(verbose)
						fprintf(stderr, "NOP\n");
					break;
				case GADGETFS_CONNECT:
					connected = 1;
					//printf("CONNECTED\n");
					current_speed = event [i].u.speed;
					if(verbose)
						fprintf(stderr, "CONNECT %s\n", speed (event [i].u.speed));
					break;
				case GADGETFS_SETUP:
					s_iCounter++;
					connected = 1;
					handle_control(fd, &event [i].u.setup, s_iCounter);
					break;
				case GADGETFS_DISCONNECT:
					connected = 0;
					//printf("DISCONNECTED\n");
					current_speed = USB_SPEED_UNKNOWN;
					if(verbose)
						fprintf(stderr, "DISCONNECT\n");
					stop_io ();
					break;
				case GADGETFS_SUSPEND:
					//printf("SUSPEND\n");
					if(verbose)
						fprintf(stderr, "SUSPEND\n");
					break;
				default:
					//printf("default case\n");
					fprintf(stderr, "* unhandled event %d\n", event [i].type);
				}
			}
			continue;

		case SIGINT:
			break;
		case SIGTERM:
			if(verbose) fprintf(stderr, "\nquit, sig %d\n", tmp);
			break;
		case SIGUSR1:
			break;
		case SIGUSR2:
			if(verbose) fprintf(stderr, "\npthreads sig %d\n", tmp);
			break;
		default:
			fprintf(stderr, "\nunrecognized signal %d\n", tmp);
			break;
		}
done:
		fflush (stdout);
		if(connected)
			stop_io();
		break;
	}

	if(verbose)
		fprintf(stderr, "done\n");
	fflush(stdout);

	pthread_cleanup_pop(1);
	return 0;
}
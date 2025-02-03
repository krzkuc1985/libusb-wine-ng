/*
 * Linux USB support
 *
 * Copyright (c) 2000-2003 Johannes Erdfelt <johannes@erdfelt.com>
 *
 * This library is covered by the LGPL, read LICENSE for details.
 */

#include <stdlib.h>	/* getenv, etc */
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>

#include "windef.h"
#include "winbase.h"

#include "linux.h"
#include "usbi.h"
#include "unixlib.h"

/* redefine linux ETIMEDOUT to match libusb-win32 ETIMEDOUT value */
#undef ETIMEDOUT
#define ETIMEDOUT 116

static int x_open( char * filename, int flags )
{
    struct prm_open p = { -1, filename, flags };
    WINE_UNIX_CALL( unix_open, &p );
    return p.ret;
}

static int x_close( int fd )
{
    struct prm_close p = { -1, fd };
    WINE_UNIX_CALL( unix_close, &p );
    return p.ret;
}

static int x_read( int fd, void * dst, size_t count )
{
    struct prm_read p = { -1, fd, dst, count };
    WINE_UNIX_CALL( unix_read, &p );
    return p.ret;
}

static int x_ioctl( int fd, unsigned long id, void * arg )
{
    struct prm_ioctl p = { -1, fd, id, arg };
    WINE_UNIX_CALL( unix_ioctl, &p );
    return p.ret;
}

static char usb_path[LIBUSB_PATH_MAX + 1] = "";

static int device_open(struct usb_device *dev)
{
    char filename[LIBUSB_PATH_MAX + 1];
    int fd;

    snprintf( filename, sizeof(filename) - 1, "%s/%s/%s", usb_path, dev->bus->dirname, dev->filename );

    fd = x_open( filename, O_RDWR );
    if( fd < 0 ) fd = x_open( filename, O_RDONLY );

    return fd;
}

int usb_os_open(usb_dev_handle *dev)
{
  dev->fd = device_open(dev->device);

  return 0;
}

int usb_os_close(usb_dev_handle *dev)
{
  if (dev->fd < 0)
    return 0;

  if (x_close(dev->fd) == -1)
    /* Failing trying to close a file really isn't an error, so return 0 */
    USB_ERROR_STR( 0, "tried to close device fd %d: %s", dev->fd, strerror(errno) );

  return 0;
}

int usb_set_configuration(usb_dev_handle *dev, int configuration)
{
    struct prm_usb_claim_interface p = { -1, dev->fd, configuration };
    WINE_UNIX_CALL( unix_usb_set_configuration, &p );
    if( p.ret < 0 ) return p.ret;
    dev->config = configuration;
    return 0;
}

int usb_claim_interface( usb_dev_handle *dev, int interface )
{
    struct prm_usb_claim_interface p = { -1, dev->fd, interface };
    WINE_UNIX_CALL( unix_usb_claim_interface, &p );
    if( p.ret < 0 ) return p.ret;
    dev->interface = interface;
    return 0;
}

int usb_release_interface( usb_dev_handle *dev, int interface )
{
    struct prm_usb_release_interface p = { -1, dev->fd, interface };
    WINE_UNIX_CALL( unix_usb_release_interface, &p );
    if( p.ret < 0 ) return p.ret;
    dev->interface = -1;
    return 0;
}

int usb_set_altinterface( usb_dev_handle *dev, int alternate )
{
    struct prm_usb_set_altinterface p = { -1, dev->fd, dev->interface, alternate };
    WINE_UNIX_CALL( unix_usb_set_altinterface, &p );
    if( p.ret < 0 ) return p.ret;
    dev->altsetting = alternate;
    return 0;
}

/*
 * Linux usbfs has a limit of one page size for synchronous bulk read/write.
 * 4096 is the most portable maximum we can do for now.
 * Linux usbfs has a limit of 16KB for the URB interface. We use this now
 * to get better performance for USB 2.0 devices.
 */

int usb_control_msg(usb_dev_handle *dev, int requesttype, int request, int value, int index, char *bytes, int size, int timeout)
{
    struct prm_usb_control_msg p = { -1, dev->fd, requesttype, request, value, index, bytes, size, timeout };
    WINE_UNIX_CALL( unix_usb_control_msg, &p );
    return p.ret;
}

/* Reading and writing are the same except for the endpoint */
static int usb_urb_transfer(usb_dev_handle *dev, int ep, int urbtype, char *bytes, int size, int timeout)
{
    struct prm_usb_urb_transfer p = { -1, dev->fd, ep, urbtype, bytes, size, timeout };
    WINE_UNIX_CALL( unix_usb_urb_transfer, &p );
    return p.ret;
}

int usb_bulk_write(usb_dev_handle *dev, int ep, char *bytes, int size, int timeout)
{
    return usb_urb_transfer(dev, ep, USB_URB_TYPE_BULK, bytes, size, timeout);
}

int usb_bulk_read(usb_dev_handle *dev, int ep, char *bytes, int size, int timeout)
{
    /* Ensure the endpoint address is correct */
    ep |= USB_ENDPOINT_IN;
    return usb_urb_transfer(dev, ep, USB_URB_TYPE_BULK, bytes, size, timeout);
}

/*
 * FIXME: Packetize large buffers here. 2.4 HCDs (atleast, haven't checked
 * 2.5 HCDs yet) don't handle multi-packet Interrupt transfers. So we need
 * to lookup the endpoint packet size and packetize appropriately here.
 */
int usb_interrupt_write(usb_dev_handle *dev, int ep, char *bytes, int size, int timeout)
{
    return usb_urb_transfer(dev, ep, USB_URB_TYPE_INTERRUPT, bytes, size, timeout);
}

int usb_interrupt_read(usb_dev_handle *dev, int ep, char *bytes, int size, int timeout)
{
    /* Ensure the endpoint address is correct */
    ep |= USB_ENDPOINT_IN;
    return usb_urb_transfer(dev, ep, USB_URB_TYPE_INTERRUPT, bytes, size, timeout);
}

int usb_os_find_busses( struct usb_bus **busses )
{
    struct usb_bus *fbus = NULL;
    char dirpath[LIBUSB_PATH_MAX + 1];
    WIN32_FIND_DATAA ffd;
    char *d_name = ffd.cFileName;
    HANDLE hFind;

    snprintf( dirpath, LIBUSB_PATH_MAX, "//?/unix/%s/*", usb_path );

    hFind = FindFirstFileA( dirpath, &ffd );

    if( hFind == INVALID_HANDLE_VALUE )
	USB_ERROR_STR( -errno, "couldn't opendir(%s): %lX", dirpath, GetLastError() );

    do {
	struct usb_bus *bus;

	/* Skip anything starting with a . */
	if( d_name[0] == '.' ) continue;
	/* Skip anything not ending with digit */
	if( !strchr( "0123456789", d_name[strlen(d_name) - 1] ) ) continue;

	bus = malloc(sizeof(*bus));
	if( !bus ) USB_ERROR( -ENOMEM );

	memset((void *)bus, 0, sizeof(*bus));
	lstrcpynA( bus->dirname, d_name, sizeof(bus->dirname) - 1 );
	bus->dirname[sizeof(bus->dirname) - 1] = 0;
	LIST_ADD( fbus, bus );

	if( usb_debug >= 2 ) fprintf(stderr, "usb_os_find_busses: Found %s\n", bus->dirname);

  } while( FindNextFileA( hFind, &ffd ) );

  FindClose( hFind );

  *busses = fbus;

  return 0;
}

int usb_os_find_devices(struct usb_bus *bus, struct usb_device **devices)
{
    struct usb_device *fdev = NULL;
    char dirpath[LIBUSB_PATH_MAX + 1];
    WIN32_FIND_DATAA ffd;
    char *d_name = ffd.cFileName;
    HANDLE hFind;

    snprintf( dirpath, LIBUSB_PATH_MAX, "//?/unix/%s/%s/*", usb_path, bus->dirname );

    hFind = FindFirstFileA( dirpath, &ffd );

    if( hFind == INVALID_HANDLE_VALUE )
	USB_ERROR_STR( -errno, "couldn't opendir(%s): %lX", dirpath, GetLastError() );

    do {
	unsigned char device_desc[DEVICE_DESC_LENGTH];
	char filename[LIBUSB_PATH_MAX + 1];
	struct usb_device *dev;
	struct usb_connectinfo connectinfo;
	int i, fd, ret;

	/* Skip anything starting with a . */
	if( d_name[0] == '.' ) continue;

	dev = malloc(sizeof(*dev));
	if( !dev ) USB_ERROR( -ENOMEM );

	memset((void *)dev, 0, sizeof(*dev));

	dev->bus = bus;

	lstrcpynA( dev->filename, d_name, sizeof(dev->filename) - 1 );
	dev->filename[sizeof(dev->filename) - 1] = 0;

	snprintf( filename, sizeof(filename) - 1, "%s/%s/%s", usb_path, bus->dirname, d_name );
	fd = x_open( filename, O_RDWR );
	if( fd < 0 )
	{
	    fd = x_open( filename, O_RDONLY );
	    if( fd < 0 )
	    {
		if( usb_debug >= 2 )
		    fprintf( stderr, "usb_os_find_devices: Couldn't open %s\n", filename );
		free(dev);
		continue;
	    }
	}

	/* Get the device number */
	ret = x_ioctl( fd, X_IOCTL_USB_CONNECTINFO, &connectinfo );
	if( ret < 0 )
	{
	    if( usb_debug )
		fprintf(stderr, "usb_os_find_devices: couldn't get connect info\n");
	}
	else
	    dev->devnum = connectinfo.devnum;

	ret = x_read( fd, (void *)device_desc, DEVICE_DESC_LENGTH );
	if( ret < 0 ) 
	{
	    if( usb_debug )
		fprintf(stderr, "usb_os_find_devices: Couldn't read descriptor\n");
	    free(dev);
	    goto err;
	}

	/*
	 * Linux kernel converts the words in this descriptor to CPU endian, so
	 * we use the undocumented W character for usb_parse_descriptor() that
	 * doesn't convert endianess when parsing the descriptor
	 */
	usb_parse_descriptor( device_desc, "bbWbbbbWWWbbbb", &dev->descriptor );

	LIST_ADD( fdev, dev );

	if( usb_debug >= 2 )
	    fprintf( stderr, "usb_os_find_devices: Found %s on %s\n", dev->filename, bus->dirname );

	/* Now try to fetch the rest of the descriptors */
	if( dev->descriptor.bNumConfigurations > USB_MAXCONFIG )
	    goto err; /* Silent since we'll try again later */

	if( dev->descriptor.bNumConfigurations < 1 )
	    goto err; /* Silent since we'll try again later */

	dev->config = malloc( dev->descriptor.bNumConfigurations * sizeof(struct usb_config_descriptor) );
	if( !dev->config )
	    goto err; /* Silent since we'll try again later */

	memset( dev->config, 0, dev->descriptor.bNumConfigurations * sizeof(struct usb_config_descriptor) );

	for (i = 0; i < dev->descriptor.bNumConfigurations; i++)
	{
	    unsigned char buffer[8], *bigbuffer;
	    struct usb_config_descriptor config;

	    /* Get the first 8 bytes so we can figure out what the total length is */
	    ret = x_read( fd, (void *)buffer, 8 );
	    if( ret < 8 )
	    {
		if( usb_debug >= 1 )
		{
		    if( ret < 0 )
			fprintf( stderr, "Unable to get descriptor (%d)\n", ret );
		    else
			fprintf( stderr, "Config descriptor too short (expected %d, got %d)\n", 8, ret );
		}
		goto err;
	    }

	    usb_parse_descriptor( buffer, "bbw", &config );

	    bigbuffer = malloc( config.wTotalLength );
	    if( !bigbuffer )
	    {
		if( usb_debug >= 1 )
		    fprintf(stderr, "Unable to allocate memory for descriptors\n");
		goto err;
	    }

	    /* Read the rest of the config descriptor */
	    memcpy( bigbuffer, buffer, 8 );

	    ret = x_read( fd, (void *)(bigbuffer + 8), config.wTotalLength - 8 );
	    if( ret < config.wTotalLength - 8 )
	    {
		if( usb_debug >= 1 )
		{
		    if( ret < 0 )
			fprintf( stderr, "Unable to get descriptor (%d)\n", ret );
		    else
			fprintf( stderr, "Config descriptor too short (expected %d, got %d)\n", config.wTotalLength, ret );
		}

		free(bigbuffer);
		goto err;
	    }

	    ret = usb_parse_configuration( &dev->config[i], bigbuffer );
	    if( usb_debug >= 2 )
	    {
		if( ret > 0 )
		    fprintf(stderr, "Descriptor data still left\n");
		else if( ret < 0 )
		    fprintf(stderr, "Unable to parse descriptors\n");
	    }
	    free(bigbuffer);
	}
err:
	x_close(fd);

    } while( FindNextFileA( hFind, &ffd ) );

    FindClose( hFind );

    *devices = fdev;

    return 0;
}

int usb_os_determine_children(struct usb_bus *bus)
{
  struct usb_device *dev, *devices[256];
  struct usb_ioctl command;
  int ret, i, i1;

  /* Create a list of devices first */
  memset(devices, 0, sizeof(devices));
  for (dev = bus->devices; dev; dev = dev->next)
    if (dev->devnum)
      devices[dev->devnum] = dev;

  /* Now fetch the children for each device */
  for (dev = bus->devices; dev; dev = dev->next) {
    struct usb_hub_portinfo portinfo;
    int fd;

    fd = device_open(dev);
    if( fd < 0 ) continue;

    /* Query the hub driver for the children of this device */
    if (dev->config && dev->config->interface && dev->config->interface->altsetting)
      command.ifno = dev->config->interface->altsetting->bInterfaceNumber;
    else
      command.ifno = 0;
    command.ioctl_code = X_IOCTL_USB_HUB_PORTINFO;
    command.data = &portinfo;
    ret = x_ioctl(fd, X_IOCTL_USB_IOCTL, &command);
    if (ret < 0) {
      /* errno == ENOSYS means the device probably wasn't a hub */
      if (errno != ENOSYS && usb_debug > 1)
        fprintf(stderr, "error obtaining child information: %s\n", strerror(errno));
      x_close(fd);
      continue;
    }

    dev->num_children = 0;
    for (i = 0; i < portinfo.numports; i++)
      if (portinfo.port[i])
        dev->num_children++;

    /* Free any old children first */
    free(dev->children);

    dev->children = malloc(sizeof(struct usb_device *) * dev->num_children);
    if (!dev->children) {
      if (usb_debug > 1)
        fprintf(stderr, "error allocating %lu bytes memory for dev->children\n",
                (unsigned long)sizeof(struct usb_device *) * dev->num_children);

      dev->num_children = 0;
      x_close(fd);
      continue;
    }

    for (i = 0, i1 = 0; i < portinfo.numports; i++) {
      if (!portinfo.port[i])
        continue;

      dev->children[i1++] = devices[portinfo.port[i]];

      devices[portinfo.port[i]] = NULL;
    }

    x_close(fd);
  }

  /*
   * There should be one device left in the devices list and that should be
   * the root device
   */
  for (i = 0; i < sizeof(devices) / sizeof(devices[0]); i++) {
    if (devices[i])
      bus->root_dev = devices[i];
  }

  return 0;
}

static int check_usb_vfs( const char *dirname )
{
    int found = 0;
    char dirpath[LIBUSB_PATH_MAX + 1];
    WIN32_FIND_DATAA ffd;
    char *d_name = ffd.cFileName;
    HANDLE hFind;

    snprintf( dirpath, LIBUSB_PATH_MAX, "//?/unix/%s", dirname );

    hFind = FindFirstFileA( dirpath, &ffd );

    if( hFind == INVALID_HANDLE_VALUE ) return 0;

    do {
	if( d_name[0] == '.' ) continue; /* Skip anything starting with a . */

	/* We assume if we find any files that it must be the right place */
	found = 1;
	break;
    } while( FindNextFileA( hFind, &ffd ) );

    FindClose( hFind );

    return found;
}

void usb_os_init(void)
{
  /* Find the path to the virtual filesystem */
  if (getenv("USB_DEVFS_PATH")) {
    if (check_usb_vfs(getenv("USB_DEVFS_PATH"))) {
      lstrcpynA(usb_path, getenv("USB_DEVFS_PATH"), sizeof(usb_path) - 1);
      usb_path[sizeof(usb_path) - 1] = 0;
    } else if (usb_debug)
      fprintf(stderr, "usb_os_init: couldn't find USB VFS in USB_DEVFS_PATH\n");
  }

  if (!usb_path[0]) {
    if (check_usb_vfs("/dev/bus/usb")) {
      lstrcpynA(usb_path, "/dev/bus/usb", sizeof(usb_path) - 1);
      usb_path[sizeof(usb_path) - 1] = 0;
    } else
      usb_path[0] = 0;	/* No path, no USB support */
  }

  if (usb_debug) {
    if (usb_path[0])
      fprintf(stderr, "usb_os_init: Found USB VFS at %s\n", usb_path);
    else
      fprintf(stderr, "usb_os_init: No USB VFS found, is it mounted?\n");
  }
}

int usb_resetep( usb_dev_handle *dev, unsigned int ep )
{
    struct prm_usb_resetep p = { -1, dev->fd, ep };
    WINE_UNIX_CALL( unix_usb_resetep, &p );
    return p.ret;
}

int usb_clear_halt( usb_dev_handle *dev, unsigned int ep )
{
    struct prm_usb_clear_halt p = { -1, dev->fd, ep };
    WINE_UNIX_CALL( unix_usb_clear_halt, &p );
    return p.ret;
}

int usb_reset( usb_dev_handle *dev )
{
    struct prm_usb_reset p = { -1, dev->fd };
    WINE_UNIX_CALL( unix_usb_reset, &p );
    return p.ret;
}

int usb_get_driver_np( usb_dev_handle *dev, int interface, char *name, unsigned int namelen )
{
    struct prm_usb_get_driver_np p = { -1, dev->fd, interface, name, namelen };
    WINE_UNIX_CALL( unix_usb_get_driver_np, &p );
    return p.ret;
}

// -------------------------------------------------------------------------------
// this async functions added by some person who'd like to remain anonymous
// It was necessary to make Aerodrums application run under wine.

typedef struct {
	usb_dev_handle *dev;
	char *bytes;
	int size;
	struct usb_urb urb;
	int reaped;
} usb_context_t;

static int _usb_setup_async(usb_dev_handle *dev, void **context,
                            int urbtype,
                            unsigned char ep, int pktsize)
{
	usb_context_t **c = (usb_context_t **)context;

	/* Any error checks required here? */

	*c = malloc(sizeof(usb_context_t));

	if (!*c) {
		USB_ERROR_STR(-errno, "memory allocation error: %s\n", strerror(errno));
		return -ENOMEM;
	}

	memset(*c, 0, sizeof(usb_context_t));

	(*c)->dev = dev;
	(*c)->urb.type = urbtype;
	(*c)->urb.endpoint = ep;
	(*c)->urb.flags = 0;
	(*c)->urb.signr = 0;
	(*c)->urb.actual_length = 0;
	(*c)->urb.number_of_packets = 0;	/* don't do isochronous yet */
	(*c)->urb.usercontext = NULL;

	return 0;
}

int usb_bulk_setup_async(usb_dev_handle *dev, void **context, unsigned char ep)
{
	return _usb_setup_async(dev, context, USB_URB_TYPE_BULK, ep, 0);
}

/* Reading and writing are the same except for the endpoint */
int usb_submit_async(void *context, char *bytes, int size)
{
	int ret;

	usb_context_t *c = (usb_context_t *)context;

	c->urb.buffer = bytes;
	c->urb.buffer_length = size;
	c->urb.usercontext = c;
	c->reaped = 0;

	ret = x_ioctl(c->dev->fd, X_IOCTL_USB_SUBMITURB, &c->urb);
	if (ret < 0) {
		USB_ERROR_STR(-errno, "error submitting URB: %s", strerror(errno));
		return ret;
	}

	return 0;
}

static int _usb_reap_async(void *context, int timeout, int cancel)
{
	int rc;
	usb_context_t *c = (usb_context_t *)context, *b;
	struct usb_urb *urb;

	if (c->reaped)
		return c->urb.actual_length;

again:
	rc = x_ioctl(c->dev->fd, X_IOCTL_USB_REAPURB, &urb);
	if (rc < 0) {
		fprintf(stderr, "error reaping URB: %s", strerror(errno));
		return rc;
	}

	b = (usb_context_t *)urb->usercontext;
	if (b != c) {
		b->reaped = 1;
		goto again;
	}

#if 0
	/* Not ready for usb_cancel_async */
	if (cancel) {
		usb_cancel_async(context);
	}
#else
	(void)cancel;
#endif
	return urb->actual_length;
}

int usb_reap_async(void *context, int timeout)
{
	return _usb_reap_async(context, timeout, 1);
}

int usb_reap_async_nocancel(void *context, int timeout)
{
	return _usb_reap_async(context, timeout, 0);
}


int usb_cancel_async(void *context)
{
    int rc;
    usb_context_t *c = (usb_context_t *)context;

    printf("%s()\n", __func__);
    USB_ERROR_STR(-errno, "usb_cancel_async is not yet implemented: %s\n", strerror(errno));
#if 0
    /* NOTE that this function will cancel all pending URBs */
    /* on the same endpoint as this particular context, or even */
    /* all pending URBs for this particular device. */

    if (!c)
    {
        USBERR0("invalid context\n");
        return -EINVAL;
    }

    if (c->dev->impl_info == INVALID_HANDLE_VALUE)
    {
        USBERR0("device not open\n");
        return -EINVAL;
    }

    _usb_cancel_io(c);

    return 0;
#endif

	rc = x_ioctl(c->dev->fd, X_IOCTL_USB_DISCARDURB, &c->urb);
	if (rc < 0)
		fprintf(stderr, "error discarding URB: %s", strerror(errno));

	/*
	* When the URB is unlinked, it gets moved to the completed list and
	* then we need to reap it or else the next time we call this function,
	* we'll get the previous completion and exit early
	*/
	x_ioctl(c->dev->fd, X_IOCTL_USB_REAPURB, &context);

	return 0;
}

int usb_free_async(void **context)
{
	usb_context_t **c = (usb_context_t **)context;

	if (!*c) {
		USB_ERROR_STR(-errno, "invalid context: %s\n", strerror(errno));
		return -EINVAL;
	}

	free(*c);
	*c = NULL;

	return 0;
}

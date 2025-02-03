/*
 * Win32 libusb0 for WINE proxy functions
 *
 * Copyright Stanson<me@stanson.ch>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 * NOTES:
 *   Proxy libusb0 manager.  This manager delegates all libusb0.dll
 *   calls to a linux kernel
 */

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#include "unixlib.h"
#include "linux.h"

static inline void *ULongToPtr(uint32_t ul)
{
    return (void *)(uint64_t)ul;
}

#define IOCTL_USB_CONTROL	_IOWR('U', 0, struct usb_ctrltransfer)
#define IOCTL_USB_BULK		_IOWR('U', 2, struct usb_bulktransfer)
#define IOCTL_USB_RESETEP	_IOR('U', 3, unsigned int)
#define IOCTL_USB_SETINTF	_IOR('U', 4, struct usb_setinterface)
#define IOCTL_USB_SETCONFIG	_IOR('U', 5, unsigned int)
#define IOCTL_USB_GETDRIVER	_IOW('U', 8, struct usb_getdriver)
#define IOCTL_USB_SUBMITURB	_IOR('U', 10, struct usb_urb)
#define IOCTL_USB_DISCARDURB	_IO('U', 11)
#define IOCTL_USB_REAPURB	_IOW('U', 12, void *)
#define IOCTL_USB_REAPURBNDELAY	_IOW('U', 13, void *)
#define IOCTL_USB_CLAIMINTF	_IOR('U', 15, unsigned int)
#define IOCTL_USB_RELEASEINTF	_IOR('U', 16, unsigned int)
#define IOCTL_USB_CONNECTINFO	_IOW('U', 17, struct usb_connectinfo)
#define IOCTL_USB_IOCTL         _IOWR('U', 18, struct usb_ioctl)
#define IOCTL_USB_HUB_PORTINFO	_IOR('U', 19, struct usb_hub_portinfo)
#define IOCTL_USB_RESET		_IO('U', 20)
#define IOCTL_USB_CLEAR_HALT	_IOR('U', 21, unsigned int)
#define IOCTL_USB_DISCONNECT	_IO('U', 22)
#define IOCTL_USB_CONNECT	_IO('U', 23)

#define ETRANSFER_TIMEDOUT 116

static int _usb_control_msg( int fd, int requesttype, int request, int value, int index, char *bytes, int size, int timeout)
{
    struct usb_ctrltransfer ctrl;
    int ret;

    ctrl.bRequestType = requesttype;
    ctrl.bRequest     = request;
    ctrl.wValue       = value;
    ctrl.wIndex       = index;
    ctrl.wLength      = size;
    ctrl.data         = bytes;
    ctrl.timeout      = timeout;

    ret = ioctl( fd, IOCTL_USB_CONTROL, &ctrl );
    if( ret < 0 ) fprintf( stderr, "control message error: %s\n", strerror( errno ) );

    return ret;
}

static int _usb_detach_kernel_driver_np( int fd, int interface )
{
    struct usb_ioctl command;
    int ret;

    command.ifno = interface;
    command.ioctl_code = IOCTL_USB_DISCONNECT;
    command.data = NULL;

    ret = ioctl( fd, IOCTL_USB_IOCTL, &command );
    if( ret < 0 ) fprintf( stderr, "could not detach kernel driver from interface %d: %s\n", interface, strerror( errno ) );

    return ret;
}

static int _usb_get_driver_np( int fd, int interface, char *name, unsigned int namelen )
{
    struct usb_getdriver getdrv;
    int ret;

    getdrv.interface = interface;
    ret = ioctl( fd, IOCTL_USB_GETDRIVER, &getdrv );
    if( ret < 0 )
    {
	fprintf( stderr, "could not get bound driver: %s", strerror( errno ) );
	return ret;
    }

    strncpy( name, getdrv.driver, namelen - 1 );
    name[namelen - 1] = 0;

    return 0;
}

static int _usb_set_altinterface( int fd, int interface, int altsetting )
{
    int ret;
    struct usb_setinterface setintf;

    if( interface < 0 ) return -EINVAL;

    setintf.interface = interface;
    setintf.altsetting = altsetting;

    ret = ioctl( fd, IOCTL_USB_SETINTF, &setintf );
    if( ret < 0 ) fprintf( stderr, "could not set alt intf %d/%d: %s", interface, altsetting, strerror( errno ) );

    return ret;
}

#define MAX_READ_WRITE		(16 * 1024)
#define URB_USERCONTEXT_COOKIE	((void *)0x1)

/* Reading and writing are the same except for the endpoint */
static int _usb_urb_transfer( int fd, int ep, int urbtype, char *bytes, int size, int timeout )
{
    struct usb_urb urb;
    int bytesdone = 0, requested;
    struct timeval tv, tv_ref, tv_now;
    struct usb_urb *context;
    int ret, waiting;

    /*
     * HACK: The use of urb.usercontext is a hack to get threaded applications
     * sort of working again. Threaded support is still not recommended, but
     * this should allow applications to work in the common cases. Basically,
     * if we get the completion for an URB we're not waiting for, then we update
     * the usercontext pointer to 1 for the other threads URB and it will see
     * the change after it wakes up from the the timeout. Ugly, but it works.
     */

    /*
     * Get actual time, and add the timeout value. The result is the absolute
     * time where we have to quit waiting for an message.
     */

    gettimeofday( &tv_ref, NULL );
    tv_ref.tv_sec = tv_ref.tv_sec + timeout / 1000;
    tv_ref.tv_usec = tv_ref.tv_usec + (timeout % 1000) * 1000;

    if( tv_ref.tv_usec > 1000000 )
    {
	tv_ref.tv_usec -= 1000000;
	tv_ref.tv_sec++;
    }

    do {
	fd_set writefds;

	requested = size - bytesdone;
	if( requested > MAX_READ_WRITE ) requested = MAX_READ_WRITE;

	urb.type = urbtype;
	urb.endpoint = ep;
	urb.flags = 0;
	urb.buffer = bytes + bytesdone;
	urb.buffer_length = requested;
	urb.signr = 0;
	urb.actual_length = 0;
	urb.number_of_packets = 0;	/* don't do isochronous yet */
	urb.usercontext = NULL;

	ret = ioctl( fd, IOCTL_USB_SUBMITURB, &urb );
	if( ret < 0 )
	{
	    fprintf( stderr, "error submitting URB ep %s(%d): %s\n", ep & 0x80 ? "IN" : "OUT", ep & 0x7F, strerror(errno) );
	    return ret;
	}

	FD_ZERO( &writefds );
	FD_SET( fd, &writefds );

restart:
	waiting = 1;
	context = NULL;
	while( !urb.usercontext && ( ( ret = ioctl( fd, IOCTL_USB_REAPURBNDELAY, &context ) ) == -1 ) && waiting )
	{
	    tv.tv_sec = 0;
	    tv.tv_usec = 1000; // 1 msec
	    select( fd + 1, NULL, &writefds, NULL, &tv); //sub second wait

	    if( timeout )
	    {
		/* compare with actual time, as the select timeout is not that precise */
		gettimeofday(&tv_now, NULL);

		if( ( tv_now.tv_sec > tv_ref.tv_sec )
		 || ( ( tv_now.tv_sec == tv_ref.tv_sec )
		   && ( tv_now.tv_usec >= tv_ref.tv_usec ) ) )
		    waiting = 0;
	    }
	}

	if( context && context != &urb )
	{
	    context->usercontext = URB_USERCONTEXT_COOKIE;
	    /* We need to restart since we got a successful URB, but not ours */
	    goto restart;
	}

	/*
	 * If there was an error, that wasn't EAGAIN (no completion), then
	 * something happened during the reaping and we should return that
	 * error now
	 */
	if( ret < 0 && !urb.usercontext && errno != EAGAIN )
	    fprintf( stderr, "error reaping URB: %s\n", strerror(errno) );

	bytesdone += urb.actual_length;

    } while( ( !ret || urb.usercontext ) && bytesdone < size && urb.actual_length == requested );

    /* If the URB didn't complete in success or error, then let's unlink it */
    if( ret < 0 && !urb.usercontext )
    {
	int rc;

	if( !waiting ) rc = -ETRANSFER_TIMEDOUT;
	else           rc = urb.status;

	ret = ioctl( fd, IOCTL_USB_DISCARDURB, &urb);
	if( ret < 0 && errno != EINVAL )
	    fprintf( stderr, "error discarding URB: %s\n", strerror(errno) );

	/*
	 * When the URB is unlinked, it gets moved to the completed list and
	 * then we need to reap it or else the next time we call this function,
	 * we'll get the previous completion and exit early
	 */
	ioctl( fd, IOCTL_USB_REAPURB, &context );

	return rc;
    }

    return bytesdone;
}

static NTSTATUS wrap_open( void *args )
{
    struct prm_open *p = args;
    p->ret = open( p->name, p->flags );
    if( p->ret < 0 ) fprintf( stderr, "failed to open %s: %s", p->name, strerror(errno) );
    return p->ret >= 0 ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

static NTSTATUS wrap_close( void *args )
{
    struct prm_close *p = args;
    p->ret = close( p->fd );
    return p->ret >= 0 ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

static NTSTATUS wrap_read( void *args )
{
    struct prm_read *p = args;
    p->ret = read( p->fd, p->dst, p->count );
    return p->ret >= 0 ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

static NTSTATUS wrap_ioctl( void *args )
{
    struct prm_ioctl *p = args;
    switch( p->id )
    {
	case X_IOCTL_USB_SUBMITURB:     p->id = IOCTL_USB_SUBMITURB; break;
	case X_IOCTL_USB_REAPURBNDELAY: p->id = IOCTL_USB_REAPURBNDELAY; break;
	case X_IOCTL_USB_DISCARDURB:    p->id = IOCTL_USB_DISCARDURB; break;
	case X_IOCTL_USB_REAPURB:       p->id = IOCTL_USB_REAPURB; break;
	case X_IOCTL_USB_CONNECTINFO:   p->id = IOCTL_USB_CONNECTINFO; break;
	case X_IOCTL_USB_IOCTL:         p->id = IOCTL_USB_IOCTL; break;
	default:
	    p->ret = -1;
	    fprintf( stderr, "ioctl error: unknown ioctl 0x%X\n", p->id );
	    return STATUS_UNSUCCESSFUL;
    }
    p->ret = ioctl( p->fd, p->id, p->arg );
    if( p->ret < 0 )
	fprintf( stderr, "ioctl( %d, 0x%X, %p ) error: %s\n", p->fd, p->id, p->arg, strerror(errno) );
    return p->ret >= 0 ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

static NTSTATUS wrap_usb_urb_transfer( void *args )
{
    struct prm_usb_urb_transfer *p = args;
    p->ret = _usb_urb_transfer( p->fd, p->ep, p->urbtype, p->bytes, p->size, p->timeout );
    return p->ret >= 0 ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

static NTSTATUS wrap_usb_control_msg( void *args )
{
    struct prm_usb_control_msg *p = args;
    p->ret = _usb_control_msg( p->fd, p->requesttype, p->request, p->value, p->index, p->bytes, p->size, p->timeout );
    return p->ret >= 0 ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

static NTSTATUS wrap_usb_get_driver_np( void *args )
{
    struct prm_usb_get_driver_np *p = args;
    p->ret = _usb_get_driver_np( p->fd, p->intf, p->name, p->namelen );
    return p->ret >= 0 ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

static NTSTATUS wrap_usb_set_configuration( void *args )
{
    struct prm_usb_set_configuration *p = args;

    /* detach kernel driver for windows program ( Stanson <me@stanson.ch > ) */
    _usb_detach_kernel_driver_np( p->fd, 0 );

    p->ret = ioctl( p->fd, IOCTL_USB_SETCONFIG, &p->configuration );
    if( p->ret < 0 ) fprintf( stderr, "could not set config %d: %s", p->configuration, strerror( errno ) );
    return p->ret >= 0 ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

static NTSTATUS wrap_usb_claim_interface( void *args )
{
    struct prm_usb_claim_interface *p = args;

    /* detach kernel driver for windows program ( Stanson <me@stanson.ch > ) */
    _usb_detach_kernel_driver_np( p->fd, p->intf );

    p->ret = ioctl( p->fd, IOCTL_USB_CLAIMINTF, &p->intf );
    if( p->ret < 0 ) fprintf( stderr, "could not claim interface %d: %s\n", p->intf, strerror( errno ) );
    return p->ret >= 0 ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

static NTSTATUS wrap_usb_release_interface( void *args )
{
    struct prm_usb_release_interface *p = args;
    p->ret = ioctl( p->fd, IOCTL_USB_RELEASEINTF, &p->intf );
    if( p->ret < 0 ) fprintf( stderr, "could not release intf %d: %s", p->intf, strerror( errno ) );
    return p->ret >= 0 ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

static NTSTATUS wrap_usb_set_altinterface( void *args )
{
    struct prm_usb_set_altinterface *p = args;
    p->ret = _usb_set_altinterface( p->fd, p->intf, p->altsetting );
    return p->ret >= 0 ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

static NTSTATUS wrap_usb_resetep( void *args )
{
    struct prm_usb_resetep *p = args;
    p->ret = ioctl( p->fd, IOCTL_USB_RESETEP, &p->ep );
    if( p->ret < 0 ) fprintf( stderr, "could not reset ep %d: %s", p->ep, strerror( errno ) );
    return p->ret >= 0 ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

static NTSTATUS wrap_usb_clear_halt( void *args )
{
    struct prm_usb_clear_halt *p = args;
    p->ret = ioctl( p->fd, IOCTL_USB_CLEAR_HALT, &p->ep );
    if( p->ret < 0 ) fprintf( stderr, "could not clear halt ep %d: %s", p->ep, strerror( errno ) );
    return p->ret >= 0 ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

static NTSTATUS wrap_usb_reset( void *args )
{
    struct prm_usb_reset *p = args;
    p->ret = ioctl( p->fd, IOCTL_USB_RESET, NULL);
    if( p->ret < 0 ) fprintf( stderr, "could not reset: %s", strerror(errno) );
    return p->ret >= 0 ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

const unixlib_entry_t __wine_unix_call_funcs[] =
{
    wrap_open,
    wrap_close,
    wrap_read,
    wrap_ioctl,
    wrap_usb_urb_transfer,
    wrap_usb_control_msg,
    wrap_usb_set_configuration,
    wrap_usb_claim_interface,
    wrap_usb_release_interface,
    wrap_usb_set_altinterface,
    wrap_usb_resetep,
    wrap_usb_clear_halt,
    wrap_usb_reset,
    wrap_usb_get_driver_np,
};

#ifdef _WIN64

static NTSTATUS wow64_open( void *args )
{
    struct p32_open *p = args;
    p->ret = open( ULongToPtr( p->name ), p->flags );
    return p->ret >= 0 ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL ;
}

static NTSTATUS wow64_read( void *args )
{
    struct p32_read *p = args;
    p->ret = read( p->fd, ULongToPtr( p->dst ), p->count );
    return p->ret >= 0 ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

static NTSTATUS wow64_ioctl( void *args )
{
    struct p32_ioctl *p32 = args;
    struct prm_ioctl p = { -1, p32->fd, p32->id, ULongToPtr( p32->arg ) };
    struct usb_ioctl c;
    struct usb_ioctl_32 *c32;
    switch( p32->id )
    {
	case IOCTL_USB_IOCTL:
	    c32 = ULongToPtr( p32->arg );
	    c.ifno = c32->ifno;
	    c.ioctl_code = c32->ioctl_code;
	    c.data = ULongToPtr( c32->data );
	    p.arg = &c;
	    break;
    }
    wrap_ioctl( &p );
    p32->ret = p.ret;
    return p32->ret >= 0 ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

static NTSTATUS wow64_usb_urb_transfer( void *args )
{
    struct p32_usb_urb_transfer *p = args;
    p->ret = _usb_urb_transfer( p->fd, p->ep, p->urbtype, ULongToPtr( p->bytes ), p->size, p->timeout );
    return p->ret >= 0 ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

static NTSTATUS wow64_usb_control_msg( void *args )
{
    struct p32_usb_control_msg *p = args;
    p->ret = _usb_control_msg( p->fd, p->requesttype, p->request, p->value, p->index, ULongToPtr( p->bytes ), p->size, p->timeout );
    return p->ret >= 0 ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

static NTSTATUS wow64_usb_get_driver_np( void *args )
{
    struct p32_usb_get_driver_np *p = args;
    p->ret = _usb_get_driver_np( p->fd, p->intf, ULongToPtr( p->name ), p->namelen );
    return p->ret >= 0 ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

const unixlib_entry_t __wine_unix_call_wow64_funcs[] =
{
    wow64_open,
    wrap_close,
    wow64_read,
    wow64_ioctl,
    wow64_usb_urb_transfer,
    wow64_usb_control_msg,
    wrap_usb_set_configuration,
    wrap_usb_claim_interface,
    wrap_usb_release_interface,
    wrap_usb_set_altinterface,
    wrap_usb_resetep,
    wrap_usb_clear_halt,
    wrap_usb_reset,
    wow64_usb_get_driver_np,
};

#endif  /* _WIN64 */

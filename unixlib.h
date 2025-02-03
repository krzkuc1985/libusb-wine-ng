/*
 * Win32 libusb0 functions
 *
 * Copyright Andrew Kozin
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
 *   calls to a real libusb-0.1.so
 */

#ifndef __UNIXLIB_H
#define __UNIXLIB_H

#include <stdarg.h>
#include <stdint.h>

#ifndef WINE_NTSTATUS_DECLARED
#define WINE_NTSTATUS_DECLARED
typedef long NTSTATUS;
#endif

#define STATUS_SUCCESS                   ((NTSTATUS) 0x00000000)
#define STATUS_UNSUCCESSFUL              ((NTSTATUS) 0xC0000001)

typedef uint64_t unixlib_handle_t;

#ifdef WINE_UNIX_LIB

typedef NTSTATUS (*unixlib_entry_t)( void *args );

extern __attribute__((visibility ("default"))) const unixlib_entry_t __wine_unix_call_funcs[];
extern __attribute__((visibility ("default"))) const unixlib_entry_t __wine_unix_call_wow64_funcs[];

#else

NTSYSAPI NTSTATUS WINAPI __wine_unix_call( unixlib_handle_t handle, unsigned int code, void *args );
extern unixlib_handle_t __wine_unixlib_handle;
extern NTSTATUS (WINAPI *__wine_unix_call_dispatcher)( unixlib_handle_t, unsigned int, void * );
extern NTSTATUS WINAPI __wine_init_unix_call(void);

#define WINE_UNIX_CALL(code,args) __wine_unix_call_dispatcher( __wine_unixlib_handle, (code), (args) )

#endif

enum unix_func
{
    unix_open,
    unix_close,
    unix_read,
    unix_ioctl,
    unix_usb_urb_transfer,
    unix_usb_control_msg,
    unix_usb_set_configuration,
    unix_usb_claim_interface,
    unix_usb_release_interface,
    unix_usb_set_altinterface,
    unix_usb_resetep,
    unix_usb_clear_halt,
    unix_usb_reset,
    unix_usb_get_driver_np,
};

//int wrap_open( char * filename, int flags );
struct prm_open { int ret; char * name; int flags; };
struct p32_open { int ret; uint32_t name; int flags; };
//int wrap_close( int fd );
struct prm_close { int ret; int fd; };
//int read( int fd, void *dst, size_t count );
struct prm_read { int ret; int fd; void * dst; size_t count; };
struct p32_read { int ret; int fd; uint32_t dst; uint32_t count; };
//int wrap_ioctl( int fd, unsigned long id, void * arg );
struct prm_ioctl { int ret; int fd; uint32_t id; void * arg; };
struct p32_ioctl { int ret; int fd; uint32_t id; uint32_t arg; };
//int _usb_urb_transfer( int fd, int ep, int urbtype, char *bytes, int size, int timeout )
struct prm_usb_urb_transfer { int ret; int fd; int ep; int urbtype; char * bytes; int size; int timeout; };
struct p32_usb_urb_transfer { int ret; int fd; int ep; int urbtype; uint32_t bytes; int size; int timeout; };
//int _usb_control_msg( int fd, int requesttype, int request, int value, int index, char *bytes, int size, int timeout)
struct prm_usb_control_msg { int ret; int fd; int requesttype; int request; int value; int index; char * bytes; int size; int timeout; };
struct p32_usb_control_msg { int ret; int fd; int requesttype; int request; int value; int index; uint32_t bytes; int size; int timeout; };
//int _usb_set_configuration( int fd, int configuration )
struct prm_usb_set_configuration { int ret; int fd; int configuration; };
//int _usb_claim_interface( int fd, int intf )
struct prm_usb_claim_interface { int ret; int fd; int intf; };
//int _usb_release_interface( int fd, int intf )
struct prm_usb_release_interface { int ret; int fd; int intf; };
//int _usb_set_altinterface( int fd, int intf, int altsetting )
struct prm_usb_set_altinterface { int ret; int fd; int intf; int altsetting; };
//int _usb_resetep( int fd, unsigned int ep )
struct prm_usb_resetep { int ret; int fd; unsigned int ep; };
//int _usb_clear_halt( int fd, unsigned int ep )
struct prm_usb_clear_halt { int ret; int fd; unsigned int ep; };
//int _usb_reset( int fd )
struct prm_usb_reset { int ret; int fd; };
//int usb_get_driver_np( usb_dev_handle *dev, int intf, char *name, unsigned int namelen )
struct prm_usb_get_driver_np { int ret; int fd; int intf; char * name; unsigned int namelen; };
struct p32_usb_get_driver_np { int ret; int fd; int intf; uint32_t name; unsigned int namelen; };

#endif

This is libusb-win32 for Wine with WoW64, i.e. Wine > 9.0.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

Some Windows applications use libusb-win32. This Wine DLL allows to run
such Windows applications under Wine using native Linux USB stack.

Prerequisites: Wine with winegcc and winebuild

Building and installing:

Set makefile variables

    WINELIB = /usr/lib64/wine
    WINEINC = /usr/include/wine

according to your actual paths to Wine libraries and includes

    $ make
    $ make install

Don't forget to copy `i386-windows/libusb0.dll` and `x86_64-windows/libusb0.dll` to
`${WINEPREFIX}/drive_c//windows/syswow64` and 
`${WINEPREFIX}/drive_c//windows/system32` respectively 
if you already have initialized WINEPREFIX.

I didn't port following libusb-win32 functions to libusb-wine:

 * usb_install_service_np
 * usb_uninstall_service_np
 * usb_install_driver_np
 * usb_isochronous_setup_async
 * usb_interrupt_setup_async

however, all Windows applications I tested with libusb-wine do not use them.

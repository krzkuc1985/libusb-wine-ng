This is libusb-win32 for Wine with WoW64, i.e. Wine > 9.0.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

Some Windows applications use libusb-win32. This Wine DLL allows to run
such Windows applications under Wine using native Linux USB stack.

New WoW64 feature of Wine allow you to run 32-bit apps on 64-bit only Linux system without
need for any multilib packages. This libusb-wine-ng sorces will build both 64-bit and 32-bit
versions of libusb0.dll

Prerequisites: Wine with winegcc and winebuild and clang

clang is used as compiler for both i386 and x86_64 targets because on 64bit systems
gcc could have no i386 libgcc compiled. You will have to install gcc-multilib or
whatever to build 32-bit version of libusb0.dll. Clang usually don't need any additional
32-bit libraries even on 64-bit only systems if compiled with i686-windows and
x86_86-windows archs.

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
if you already have initialized `${WINEPREFIX}`.

If you know how to force Wine to add new dlls installed in `/usr/lib64/wine` to
`${WINEPREFIX}` without rebuilding whole `${WINEPREFIX}`, please feel free to
add an issue with solution.

I didn't port following libusb-win32 functions to libusb-wine:

 * usb_install_service_np
 * usb_uninstall_service_np
 * usb_install_driver_np
 * usb_isochronous_setup_async
 * usb_interrupt_setup_async

however, all Windows applications I tested with libusb-wine do not use them.

Good luck.

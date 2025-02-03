WINELIB = /usr/lib64/wine
WINEINC = /usr/include/wine

WINE_INCLUDES = -I$(WINEINC)/windows -I$(WINEINC)/msvcrt

CC = gcc
CFLAGS = \
    -m64 \
    -O2 \
    -pipe \
    -fcf-protection=none \
    -fvisibility=hidden \
    -fno-stack-protector \
    -fno-strict-aliasing \
    -fPIC \
    -fasynchronous-unwind-tables \
    -Wall \
    -Wdeclaration-after-statement \
    -Wempty-body \
    -Wignored-qualifiers \
    -Winit-self \
    -Wno-packed-not-aligned \
    -Wshift-overflow=2 \
    -Wstrict-prototypes \
    -Wtype-limits \
    -Wunused-but-set-parameter \
    -Wvla \
    -Wwrite-strings \
    -Wpointer-arith \
    -Wlogical-op
LDFLAGS = \
    -shared \
    -Wl,-Bsymbolic \
    -Wl,-soname,libusb0.so \
    -Wl,-z,defs \
    -L/usr/lib64 -ldl \
    -L$(WINELIB)/x86_64-unix -l:ntdll.so

i386_CC = clang
i386_CFLAGS = \
    -O2 \
    -D__STDC__ \
    -D__WINE_PE_BUILD \
    -target i686-windows \
    -fuse-ld=lld \
    --no-default-config \
    -fno-strict-aliasing \
    -fno-omit-frame-pointer \
    -Wall \
    -Wdeclaration-after-statement \
    -Wempty-body \
    -Wignored-qualifiers \
    -Winit-self \
    -Wno-pragma-pack \
    -Wno-microsoft-enum-forward-reference \
    -Wstrict-prototypes \
    -Wtype-limits \
    -Wunused-but-set-parameter \
    -Wvla \
    -Wwrite-strings \
    -Wpointer-arith \
    -Wabsolute-value \
    -Wenum-conversion
i386_LDFLAGS = --no-default-config -L$(WINELIB)/i386-windows

x86_64_CC = x86_64-w64-mingw32-gcc
x86_64_CFLAGS = \
    -O2 \
    -D__WINE_PE_BUILD \
    -fno-strict-aliasing \
    -Wall \
    -Wdeclaration-after-statement \
    -Wempty-body \
    -Wignored-qualifiers \
    -Winit-self \
    -Wno-packed-not-aligned \
    -Wshift-overflow=2 \
    -Wstrict-prototypes \
    -Wtype-limits \
    -Wunused-but-set-parameter \
    -Wvla \
    -Wwrite-strings \
    -Wpointer-arith \
    -Wlogical-op \
    -Wabsolute-value \
    -Wformat-overflow \
    -Wnonnull \
    -mcx16 \
    -mcmodel=small -gdwarf-4
x86_64_LDFLAGS = -L$(WINELIB)/x86_64-windows

LIBS = -lwinecrt0 -lucrtbase -lkernel32 -lntdll

all: libusb0.so i386-windows/libusb0.dll x86_64-windows/libusb0.dll

i386-windows:
	mkdir -p i386-windows

x86_64-windows:
	mkdir -p x86_64-windows

unixlib.o: unixlib.c
	$(CC) -c -o $@ $< -D__WINESRC__ -DWINE_UNIX_LIB -D_WIN64 $(CFLAGS)

i386-windows/%.o: %.c i386-windows
	$(i386_CC) -c -o $@ $< $(WINE_INCLUDES) -D_UCRT -D__WINESRC__ $(i386_CFLAGS)

x86_64-windows/%.o: %.c x86_64-windows
	$(x86_64_CC) -c -o $@ $< $(WINE_INCLUDES) -D_UCRT -D__WINESRC__ $(x86_64_CFLAGS)

libusb0.a: libusb0.spec
	winebuild -w --implib -o $@ -m64 --export $^

i386-windows/libusb0.a: libusb0.spec
	winebuild -w --implib -o $@ -b i686-windows --export $^

x86_64-windows/libusb0.a: libusb0.spec
	winebuild -w --implib -o $@ --without-dlltool -b x86_64-w64-mingw32 --export $^

i386-windows/libusb0.dll: libusb0.spec i386-windows/usb-wine.o i386-windows/usb.o \
  i386-windows/descriptors.o i386-windows/error.o i386-windows/linux.o
	winegcc -o $@ $^ -b i686-windows -Wl,--wine-builtin -shared $(LIBS) $(i386_LDFLAGS)

x86_64-windows/libusb0.dll: libusb0.spec x86_64-windows/usb-wine.o x86_64-windows/usb.o \
  x86_64-windows/descriptors.o x86_64-windows/error.o x86_64-windows/linux.o
	winegcc -o $@ $^ -b x86_64-w64-mingw32 -Wl,--wine-builtin -shared $(LIBS) $(x86_64_LDFLAGS)

libusb0.so: unixlib.o
	$(CC) -o $@ $^ $(LDFLAGS)

install install-lib:: i386-windows/libusb0.dll x86_64-windows/libusb0.dll libusb0.so
	STRIPPROG=i686-windows-strip install-sh -m 644 $(INSTALL_PROGRAM_FLAGS) i386-windows/libusb0.dll $(DESTDIR)$(dlldir)/i386-windows/libusb0.dll
	winebuild --builtin $(DESTDIR)$(dlldir)/i386-windows/libusb0.dll
	STRIPPROG=x86_64-w64-mingw32-strip install-sh -m 644 $(INSTALL_PROGRAM_FLAGS) x86_64-windows/libusb0.dll $(DESTDIR)$(dlldir)/x86_64-windows/libusb0.dll
	winebuild --builtin $(DESTDIR)$(dlldir)/x86_64-windows/libusb0.dll
	STRIPPROG="$(STRIP)" install-sh $(INSTALL_PROGRAM_FLAGS) libusb0.so $(DESTDIR)$(dlldir)/x86_64-unix/libusb0.so

install install-dev:: libusb0.a i386-windows/libusb0.a x86_64-windows/libusb0.a
	../tools/install-sh -m 644 $(INSTALL_DATA_FLAGS) libusb0.a $(DESTDIR)$(dlldir)/x86_64-unix/libusb0.a
	../tools/install-sh -m 644 $(INSTALL_DATA_FLAGS) i386-windows/libusb0.a $(DESTDIR)$(dlldir)/i386-windows/libusb0.a
	../tools/install-sh -m 644 $(INSTALL_DATA_FLAGS) x86_64-windows/libusb0.a $(DESTDIR)$(dlldir)/x86_64-windows/libusb0.a

clean::
	rm -f libusb0.a i386-windows/libusb0.a x86_64-windows/libusb0.a \
  libusb0.so unixlib.o \
  i386-windows/*.o i386-windows/*.dll \
  x86_64-windows/*.o x86_64-windows/*.dll

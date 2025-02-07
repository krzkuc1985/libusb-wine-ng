WINELIB = /usr/lib64/wine
WINEINC = /usr/include/wine

WINE_INCLUDES = -I$(WINEINC)/windows -I$(WINEINC)/msvcrt

CC = clang
CFLAGS = \
    -m64 \
    -O2 \
    -D__WINESRC__ \
    -DWINE_UNIX_LIB \
    -D_WIN64 \
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
    -Wno-pragma-pack \
    -Wstrict-prototypes \
    -Wtype-limits \
    -Wunused-but-set-parameter \
    -Wvla \
    -Wwrite-strings \
    -Wpointer-arith
LDFLAGS = \
    -shared \
    -Wl,-Bsymbolic \
    -Wl,-soname,libusb0.so \
    -Wl,-z,defs \
    -L$(WINELIB)/x86_64-unix -l:ntdll.so

WIN_LIBS = -lwinecrt0 -lucrtbase -lkernel32 -lntdll

i386_CC = clang
i386_CFLAGS = \
    -O2 \
    -D__STDC__ \
    -D__WINE_PE_BUILD \
    -D_UCRT \
    -D__WINESRC__ \
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
    -Wenum-conversion \
    $(WINE_INCLUDES)
i386_LDFLAGS = \
    -b i686-windows \
    -Wl,--wine-builtin \
    -shared \
    --no-default-config \
    -L$(WINELIB)/i386-windows $(WIN_LIBS)
i386_DIR = i386-windows

x86_64_CC = clang
x86_64_CFLAGS = \
    -O2 \
    -D__STDC__ \
    -D__WINE_PE_BUILD \
    -D_UCRT \
    -D__WINESRC__ \
    -target x86_64-windows \
    -fuse-ld=lld \
    --no-default-config \
    -fno-strict-aliasing \
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
    -Wenum-conversion \
    -Wformat-overflow \
    -Wnonnull \
    -mcx16 \
    -mcmodel=small \
    $(WINE_INCLUDES)
x86_64_LDFLAGS = \
    -b x86_64-windows \
    -Wl,--wine-builtin \
    -shared \
    --no-default-config \
    -L$(WINELIB)/x86_64-windows $(WIN_LIBS)
x86_64_DIR = x86_64-windows

SRCS = \
    usb-wine.c \
    usb.c \
    descriptors.c \
    error.c \
    linux.c

all: libusb0.so i386-windows/libusb0.dll x86_64-windows/libusb0.dll

$(i386_DIR) $(x86_64_DIR):
	mkdir -p $@

unixlib.o: unixlib.c
	$(CC) -c -o $@ $< $(CFLAGS)

$(i386_DIR)/%.o: %.c | $(i386_DIR)
	$(i386_CC) -c -o $@ $< $(i386_CFLAGS)

$(x86_64_DIR)/%.o: %.c | $(x86_64_DIR)
	$(x86_64_CC) -c -o $@ $< $(x86_64_CFLAGS)

libusb0.a: libusb0.spec
	winebuild -w --implib -o $@ -m64 --export $^

$(i386_DIR)/libusb0.a: libusb0.spec
	winebuild -w --implib -o $@ -b i686-windows --export $^

$(x86_64_DIR)/libusb0.a: libusb0.spec
	winebuild -w --implib -o $@ --without-dlltool -b x86_64-w64-mingw32 --export $^

libusb0.so: unixlib.o
	$(CC) -o $@ $^ $(LDFLAGS)

$(i386_DIR)/libusb0.dll: libusb0.spec $(addprefix $(i386_DIR)/, $(SRCS:.c=.o))
	winegcc -o $@ $^ $(i386_LDFLAGS)

$(x86_64_DIR)/libusb0.dll: libusb0.spec $(addprefix $(x86_64_DIR)/, $(SRCS:.c=.o))
	winegcc -o $@ $^ $(x86_64_LDFLAGS)

install install-lib:: i386-windows/libusb0.dll x86_64-windows/libusb0.dll libusb0.so
	install-sh -m 644 $(INSTALL_PROGRAM_FLAGS) i386-windows/libusb0.dll $(DESTDIR)$(dlldir)/i386-windows/libusb0.dll
	winebuild --builtin $(DESTDIR)$(dlldir)/i386-windows/libusb0.dll
	install-sh -m 644 $(INSTALL_PROGRAM_FLAGS) x86_64-windows/libusb0.dll $(DESTDIR)$(dlldir)/x86_64-windows/libusb0.dll
	winebuild --builtin $(DESTDIR)$(dlldir)/x86_64-windows/libusb0.dll
	install-sh $(INSTALL_PROGRAM_FLAGS) libusb0.so $(DESTDIR)$(dlldir)/x86_64-unix/libusb0.so

install install-dev:: libusb0.a i386-windows/libusb0.a x86_64-windows/libusb0.a
	../tools/install-sh -m 644 $(INSTALL_DATA_FLAGS) libusb0.a $(DESTDIR)$(dlldir)/x86_64-unix/libusb0.a
	../tools/install-sh -m 644 $(INSTALL_DATA_FLAGS) i386-windows/libusb0.a $(DESTDIR)$(dlldir)/i386-windows/libusb0.a
	../tools/install-sh -m 644 $(INSTALL_DATA_FLAGS) x86_64-windows/libusb0.a $(DESTDIR)$(dlldir)/x86_64-windows/libusb0.a

clean::
	rm -f libusb0.a libusb0.so unixlib.o
	rm -rf $(i386_DIR) $(x86_64_DIR)

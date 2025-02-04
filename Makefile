PROTOCOL_DIR = /usr/share/wayland-protocols
SCANNER = wayland-scanner
CC = clang
#OPTIMIZE_FLAGS = -O3 -ffast-math -flto=full -march=native

all: main

clean:
	rm main
	rm olive.c
	rm stb_image.h
	rm -r protocols
	rm -r assets

protocols/xdg-shell.h:
	$(SCANNER) client-header $(PROTOCOL_DIR)/stable/xdg-shell/xdg-shell.xml protocols/xdg-shell.h
protocols/xdg-shell.c:
	$(SCANNER) private-code $(PROTOCOL_DIR)/stable/xdg-shell/xdg-shell.xml protocols/xdg-shell.c

protocols/wlr-layer-shell-unstable-v1.xml:
	wget -nv  -O protocols/wlr-layer-shell-unstable-v1.xml "https://gitlab.freedesktop.org/wlroots/wlroots/-/raw/master/protocol/wlr-layer-shell-unstable-v1.xml?ref_type=heads&inline=false"
protocols/wlr-layer-shell.h: protocols/wlr-layer-shell-unstable-v1.xml
	$(SCANNER) client-header protocols/wlr-layer-shell-unstable-v1.xml protocols/wlr-layer-shell.h
protocols/wlr-layer-shell.c: protocols/wlr-layer-shell-unstable-v1.xml
	$(SCANNER) private-code protocols/wlr-layer-shell-unstable-v1.xml protocols/wlr-layer-shell.c

protocols/wlr-screencopy-unstable-v1.xml:
	wget -nv  -O protocols/wlr-screencopy-unstable-v1.xml "https://gitlab.freedesktop.org/wlroots/wlroots/-/raw/master/protocol/wlr-screencopy-unstable-v1.xml?ref_type=heads&inline=false"
protocols/wlr-screencopy.h: protocols/wlr-screencopy-unstable-v1.xml
	$(SCANNER) client-header protocols/wlr-screencopy-unstable-v1.xml protocols/wlr-screencopy.h
protocols/wlr-screencopy.c: protocols/wlr-screencopy-unstable-v1.xml
	$(SCANNER) private-code protocols/wlr-screencopy-unstable-v1.xml protocols/wlr-screencopy.c

protocols:
	mkdir -p protocols

assets:
	mkdir -p assets
	wget -nv -O assets/plat.png https://wiki.warframe.com/images/thumb/PlatinumLarge.png/300px-PlatinumLarge.png?f57e3

olive.c:
	wget -nv -O olive.c https://raw.githubusercontent.com/tsoding/olive.c/refs/heads/master/olive.c

stb_image.h:
	wget -nv -O stb_image.h https://raw.githubusercontent.com/nothings/stb/refs/heads/master/stb_image.h

main: protocols assets main.c olive.c stb_image.h \
	protocols/xdg-shell.h protocols/xdg-shell.c\
	protocols/wlr-layer-shell.h protocols/wlr-layer-shell.c\
	protocols/wlr-screencopy.h protocols/wlr-screencopy.c
	$(CC) main.c protocols/xdg-shell.c protocols/wlr-layer-shell.c protocols/wlr-screencopy.c\
		-Wall -Wextra -Wno-unused-variable -Wno-missing-braces\
		-lwayland-client -lm -ltesseract -lleptonica\
		-ggdb $(OPTIMIZE_FLAGS) -o main

tocr: tocr.c
	$(CC) tocr.c -ltesseract -lleptonica -o tocr

APP := needle
CC ?= cc
PREFIX ?= /usr/local
PKGS := gtk+-3.0 gtk-layer-shell-0 gio-2.0 cairo libsoup-3.0
SOURCES := src/needle.c src/config.c src/render.c src/wallpaper.c src/player.c
CFLAGS += -O2 -Wall -Wextra -Wno-unused-parameter -std=c11 -DNEEDLE_INSTALL_ASSET_DIR='"$(PREFIX)/share/needle/assets"' $(shell pkg-config --cflags $(PKGS))
LDLIBS += $(shell pkg-config --libs $(PKGS)) -lm

.PHONY: all clean install
all: $(APP)

$(APP): $(SOURCES) src/needle.h
	$(CC) $(CFLAGS) $(SOURCES) -o $@ $(LDLIBS)

install: $(APP)
	install -Dm755 $(APP) $(DESTDIR)$(PREFIX)/bin/$(APP)
	install -Dm644 data/needle.desktop $(DESTDIR)$(PREFIX)/share/applications/needle.desktop
	install -Dm644 assets/listening-room.png $(DESTDIR)$(PREFIX)/share/needle/assets/listening-room.png

clean:
	rm -f $(APP)

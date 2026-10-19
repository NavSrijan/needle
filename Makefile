APP := needle
CC ?= cc
PREFIX ?= /usr/local
PKGS := gtk+-3.0 gtk-layer-shell-0 gio-2.0 cairo libsoup-3.0
SOURCES := src/needle.c src/config.c src/render.c src/scene.c src/wallpaper.c src/player.c
CFLAGS += -O2 -Wall -Wextra -Wno-unused-parameter -std=c11 $(shell pkg-config --cflags $(PKGS))
LDLIBS += $(shell pkg-config --libs $(PKGS)) -lm

.PHONY: all clean install
all: $(APP)

$(APP): $(SOURCES) src/needle.h
	$(CC) $(CFLAGS) $(SOURCES) -o $@ $(LDLIBS)

install: $(APP)
	install -Dm755 $(APP) $(DESTDIR)$(PREFIX)/bin/$(APP)
	install -Dm644 data/needle.desktop $(DESTDIR)$(PREFIX)/share/applications/needle.desktop

clean:
	rm -f $(APP)

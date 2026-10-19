APP := needle
CC ?= cc
PREFIX ?= /usr/local
PKGS := gtk+-3.0 gtk-layer-shell-0 gio-2.0 cairo libsoup-3.0
CFLAGS += -O2 -Wall -Wextra -Wno-unused-parameter -std=c11 -DNEEDLE_INSTALL_ASSET_DIR='"$(PREFIX)/share/needle/assets"' $(shell pkg-config --cflags $(PKGS))
LDLIBS += $(shell pkg-config --libs $(PKGS)) -lm

.PHONY: all clean install
all: $(APP)

$(APP): src/needle.c
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)

install: $(APP)
	install -Dm755 $(APP) $(DESTDIR)$(PREFIX)/bin/$(APP)
	install -Dm644 data/needle.desktop $(DESTDIR)$(PREFIX)/share/applications/needle.desktop
	install -Dm644 data/needle-settings.desktop $(DESTDIR)$(PREFIX)/share/applications/needle-settings.desktop
	install -Dm644 assets/after-hours.jpg $(DESTDIR)$(PREFIX)/share/needle/assets/after-hours.jpg
	install -Dm644 assets/listening-room.jpg $(DESTDIR)$(PREFIX)/share/needle/assets/listening-room.jpg
	install -Dm644 assets/studio.jpg $(DESTDIR)$(PREFIX)/share/needle/assets/studio.jpg
	install -Dm644 assets/turntable.png $(DESTDIR)$(PREFIX)/share/needle/assets/turntable.png

clean:
	rm -f $(APP)

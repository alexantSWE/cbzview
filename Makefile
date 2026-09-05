CC ?= gcc
CFLAGS ?= -O3 -Wall -Wextra
override CPPFLAGS += -D_GNU_SOURCE -DGL_GLEXT_PROTOTYPES \
                     $(shell pkg-config --cflags glfw3 libzip libturbojpeg libwebp libpng gl libavif libjxl)
LIBS := $(shell pkg-config --libs glfw3 libzip libturbojpeg libwebp libpng gl libavif libjxl) -lm -pthread

PREFIX ?= /usr
BINDIR = $(PREFIX)/bin
DATADIR = $(PREFIX)/share

SRC = src/archive.c \
      src/comicinfo.c \
      src/decoder.c \
      src/renderer.c \
      src/osd.c \
      src/bookmark.c \
      src/config.c \
      src/main.c

OBJ = $(SRC:.c=.o)
TARGET = cbzview

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJ) -o $@ $(LIBS)

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	install -Dm644 cbzview.desktop $(DESTDIR)$(DATADIR)/applications/cbzview.desktop
	install -Dm644 cbzview.svg $(DESTDIR)$(DATADIR)/icons/hicolor/scalable/apps/cbzview.svg

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -f $(DESTDIR)$(DATADIR)/applications/cbzview.desktop
	rm -f $(DESTDIR)$(DATADIR)/icons/hicolor/scalable/apps/cbzview.svg

clean:
	rm -f src/*.o $(TARGET)

.PHONY: all clean install uninstall

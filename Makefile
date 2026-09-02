CC ?= gcc
CFLAGS ?= -O3 -march=native -Wall -Wextra -D_GNU_SOURCE -DGL_GLEXT_PROTOTYPES -pthread \
         $(shell pkg-config --cflags glfw3 libzip libturbojpeg libwebp libpng gl)
LIBS = $(shell pkg-config --libs glfw3 libzip libturbojpeg libwebp libpng gl) -pthread -lm

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
	$(CC) $(OBJ) -o $@ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

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

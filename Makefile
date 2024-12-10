CC = clang
CFLAGS = -Wall -lm -lpthread \
	-Wno-int-to-pointer-cast -Wno-pointer-to-int-cast

CFLAGS += $(shell pkg-config --libs --cflags nvidia-ml)

ifdef DEBUG
CFLAGS += -g
else
CFLAGS += -O3
endif

ifdef PROFILE
CFLAGS += -D PROFILE=1
endif

build: i3status-c

install: build
	@install i3status-c /usr/local/bin/

uninstall:
	rm /usr/local/bin/i3status-c

i3status-c: main.c
	$(CC) $(CFLAGS) main.c -o i3status-c

clean:
	rm -rf i3status-c i3status-c.dSYM test


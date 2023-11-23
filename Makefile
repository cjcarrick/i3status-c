CC = gcc
CFLAGS = -Wall -lm -lpthread

# nvml
NVML_cudaroot=/usr/local/cuda-12.3
NVML_libdir=${cudaroot}/targets/x86_64-linux/lib
NVML_includedir=${cudaroot}/targets/x86_64-linux/include
CFLAGS += -I${NVML_includedir} -lnvidia-ml -L$(NVML_libdir)/stubs

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


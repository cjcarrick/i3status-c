CXX = g++
CXXFLAGS = -std=c++17 -Wall

ifdef DEBUG
CXXFLAGS += -g
else
CXXFLAGS += -O3
endif

build: i3status-c

install: build
	@install i3status-c /usr/local/bin/

uninstall:
	rm /usr/local/bin/i3status-c

i3status-c: main.cpp
	$(CXX) $(CXXFLAGS) main.cpp -o i3status-c

clean:
	rm -rf i3status-c i3status-c.dSYM


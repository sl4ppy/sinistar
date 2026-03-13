CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter
LDFLAGS = $(shell pkg-config --libs sdl2) -lm
INCLUDES = $(shell pkg-config --cflags sdl2) -Isrc

TARGET = sinistar
SRCS = src/main.cpp
HEADERS = src/assets.h src/game.h

.PHONY: all clean run debug

all: $(TARGET)

$(TARGET): $(SRCS) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $(SRCS) $(LDFLAGS)

debug: CXXFLAGS += -g -O0 -DDEBUG
debug: $(TARGET)

clean:
	rm -f $(TARGET)

run: $(TARGET)
	./$(TARGET)

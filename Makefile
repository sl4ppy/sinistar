CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter
LDFLAGS = $(shell pkg-config --libs sdl2) -lm
INCLUDES = $(shell pkg-config --cflags sdl2) -Isrc

TARGET = sinistar
SRCS = src/main.cpp src/stb_impl.cpp
HEADERS = src/assets.h src/game.h src/stb_image.h

.PHONY: all clean run debug extract

all: $(TARGET)

$(TARGET): $(SRCS) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $(SRCS) $(LDFLAGS)

debug: CXXFLAGS += -g -O0 -DDEBUG
debug: $(TARGET)

# Extract assets from ROMs (run once, requires source/ ROMs)
extract: tools/extract_assets.cpp
	$(CXX) -std=c++17 -O2 -Isrc -o extract_assets $<
	./extract_assets

clean:
	rm -f $(TARGET) extract_assets

run: $(TARGET)
	./$(TARGET)

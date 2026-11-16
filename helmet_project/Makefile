CXX ?= g++
CXXFLAGS := -std=c++17 -O2 -fPIC -Wall -Wextra
QT_CFLAGS := $(shell pkg-config --cflags Qt5Widgets)
QT_LIBS := $(shell pkg-config --libs Qt5Widgets)
TARGET := helmet_cam_demo

all: $(TARGET)

$(TARGET): src/helmet_cam_demo.cpp include/HelmetEngine.h Makefile
	$(CXX) $(CXXFLAGS) -Iinclude $(QT_CFLAGS) $< -Llib -lhelmetdet -lopencv_world $(QT_LIBS) -lpthread -ldl -Wl,-rpath-link,lib -Wl,--allow-shlib-undefined -Wl,-rpath,'$$ORIGIN/lib' -o $@

clean:
	rm -f $(TARGET)

.PHONY: all clean

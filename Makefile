CXX ?= g++
# The Lua Hyprland is built with (main.cpp refuses to load against another).
LUA_PKG ?= $(shell for p in lua5.5 lua5.4 lua; do pkg-config --exists "$$p >= 5.4" && { echo $$p; break; }; done)
CXXFLAGS ?= -O2 -g

# GCC marks inline functions' statics (e.g. Hyprland's
# __hyprland_api_get_client_hash) as unique symbols: a .so with any can't be
# unloaded, and a later load binds to the old copy's statics.
EXTRA_FLAGS =
ifeq ($(CXX),g++)
    EXTRA_FLAGS += -fno-gnu-unique
endif

SOURCES = $(wildcard src/*.cpp) .build/hyprture-v1.cpp
HEADERS = $(wildcard src/*.hpp) hyprture.h .build/hyprture-v1.hpp
PKGS = hyprland pixman-1 libdrm pangocairo libinput libudev wayland-server xkbcommon

.PHONY: all clean test qml

all: hyprture.so

.build/hyprture-v1.cpp .build/hyprture-v1.hpp &: protocol/hyprture-v1.xml
	mkdir -p .build
	hyprwayland-scanner protocol/hyprture-v1.xml .build/

hyprture.so: $(SOURCES) $(HEADERS)
	$(CXX) -shared -fPIC $(CXXFLAGS) $(EXTRA_FLAGS) -std=c++26 -Wno-narrowing -Isrc -I.build $(SOURCES) -o $@ `pkg-config --cflags $(PKGS) '$(LUA_PKG) >= 5.4'`

# The QML module (import Hyprture), in .build/qml/Hyprture: point
# QML_IMPORT_PATH at .build/qml.
qml:
	cmake -S qml -B .build/qml -DCMAKE_BUILD_TYPE=Release
	cmake --build .build/qml

# Fit.hpp needs no Hyprland: its test builds with the compiler alone.
TESTS = .build/fit-test

test: $(TESTS)
	for t in $(TESTS); do $$t || exit 1; done

.build/fit-test: tests/fit.cpp src/Fit.hpp
	mkdir -p .build
	$(CXX) $(CXXFLAGS) -std=c++26 -Isrc tests/fit.cpp -o $@

clean:
	rm -rf hyprture.so .build

CXX ?= g++
PKG_CONFIG ?= pkg-config
PACKAGES := ncursesw libutf8proc json-c openssl
CPPFLAGS += $(shell $(PKG_CONFIG) --cflags $(PACKAGES)) -Isrc
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic
LDLIBS += $(shell $(PKG_CONFIG) --libs $(PACKAGES))
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share/fvoci
SKILLDIR ?= $(if $(CODEX_HOME),$(CODEX_HOME),$(HOME)/.codex)/skills
INSTALL ?= install

TARGET := build/fvoci
SOURCES := $(wildcard src/*.cpp)
OBJECTS := $(patsubst src/%.cpp,build/%.o,$(SOURCES))
DEPS := $(OBJECTS:.o=.d)

.PHONY: all run clean test install install-skill uninstall
.DELETE_ON_ERROR:

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@

build/%.o: src/%.cpp | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

build:
	mkdir -p $@

run: $(TARGET)
	./$(TARGET)

build/tests: tests/test.cpp build/core.o build/progress.o | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ $(LDFLAGS) $(LDLIBS) -o $@

test: $(TARGET) build/tests
	./build/tests
	python3 tests/integration.py

install: $(TARGET)
	$(INSTALL) -d "$(DESTDIR)$(BINDIR)" "$(DESTDIR)$(DATADIR)"
	$(INSTALL) -m 755 $(TARGET) "$(DESTDIR)$(BINDIR)/fvoci"
	$(INSTALL) -m 644 vocab/demo.json "$(DESTDIR)$(DATADIR)/demo.json"

install-skill:
	$(INSTALL) -d "$(DESTDIR)$(SKILLDIR)/fvoci-vocab/references" "$(DESTDIR)$(SKILLDIR)/fvoci-vocab/agents"
	$(INSTALL) -m 644 skills/fvoci-vocab/SKILL.md "$(DESTDIR)$(SKILLDIR)/fvoci-vocab/SKILL.md"
	$(INSTALL) -m 644 skills/fvoci-vocab/references/* "$(DESTDIR)$(SKILLDIR)/fvoci-vocab/references/"
	$(INSTALL) -m 644 skills/fvoci-vocab/agents/openai.yaml "$(DESTDIR)$(SKILLDIR)/fvoci-vocab/agents/openai.yaml"

uninstall:
	$(RM) "$(DESTDIR)$(BINDIR)/fvoci" "$(DESTDIR)$(DATADIR)/demo.json"

clean:
	$(RM) -r build

-include $(DEPS)

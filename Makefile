# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Davidian
# Linux / BSD / macOS / MinGW build of tangerine_orbit.lv2 (plugin + GUI), and on macOS
# also the Audio Unit, build/tangerine_orbit.component.
# Windows with Visual Studio (LV2 and VST3): use build.bat instead.
#
#   make            build the bundles in build/
#   make test       build and run the offline DSP test and the panel render
#   make install    copy the bundles to the per-user plugin folders
#   make dist       zip the bundles with the docs into build/dist/

VERSION ?= dev
UNAME   := $(shell uname -s)
UNAME_M := $(shell uname -m)

CC      ?= cc
OPT     ?= -O2
WARN    ?= -Wall -Wextra
CSTD    := -std=c11 -D_DEFAULT_SOURCE
# No -ffast-math: it lets the compiler drop the NaN guard and isfinite() checks.
COMMON  := $(OPT) $(WARN) -fno-math-errno -fvisibility=hidden

ifeq ($(UNAME),Darwin)
  PLATFORM    := macos-universal
  LIB_EXT     := .dylib
  MACOS_MIN   ?= 10.13
  ARCH_FLAGS  ?= -arch arm64 -arch x86_64
  COMMON      += $(ARCH_FLAGS) -mmacosx-version-min=$(MACOS_MIN)
  SHARED      := -dynamiclib
  UI_CLASS    := ui:CocoaUI
  UI_LANG     := -x objective-c -fno-objc-arc
  UI_LIBS     := -framework Cocoa
  LV2DIR      ?= $(HOME)/Library/Audio/Plug-Ins/LV2
  AUDIR       ?= $(HOME)/Library/Audio/Plug-Ins/Components
  EXTRA       := au
  TEST_LIBS   :=
else ifneq (,$(findstring MINGW,$(UNAME)))
  PLATFORM    := windows-x64
  LIB_EXT     := .dll
  SHARED      := -shared -static-libgcc
  UI_CLASS    := ui:WindowsUI
  UI_LANG     := $(CSTD)
  UI_LIBS     := -luser32 -lgdi32
  LV2DIR      ?= $(APPDATA)/LV2
  TEST_LIBS   :=
else
  PLATFORM    := linux-$(subst aarch64,arm64,$(subst x86_64,x64,$(UNAME_M)))
  LIB_EXT     := .so
  COMMON      += -fPIC
  SHARED      := -shared -Wl,--no-undefined
  UI_CLASS    := ui:X11UI
  UI_LANG     := $(CSTD)
  UI_LIBS     := -lX11
  LV2DIR      ?= $(HOME)/.lv2
  TEST_LIBS   := -ldl
endif

BUNDLE  := build/tangerine_orbit.lv2
DISTDIR := build/dist
DISTNAME:= davidian-tangerine-orbit-$(VERSION)-$(PLATFORM)
DOCS    := LICENSE NOTICE.md AI_USAGE.md

PLUGIN  := $(BUNDLE)/tangerine_orbit$(LIB_EXT)
UI      := $(BUNDLE)/tangerine_orbit_ui$(LIB_EXT)
DSP_SRC := src/plugin.c src/engine.h src/allpass.h src/circuit.h src/input_buffer.h src/lfo.h \
           src/mix_amp.h src/phaser.h src/lv2_abi.h src/ports.h

all: $(PLUGIN) $(UI) $(BUNDLE)/manifest.ttl $(BUNDLE)/tangerine_orbit.ttl $(addprefix $(BUNDLE)/,$(DOCS))

# The Audio Unit (macOS): one bundle with the engine, the parameters and the same GUI.
COMPONENT := build/tangerine_orbit.component
AU_BIN    := $(COMPONENT)/Contents/MacOS/tangerine_orbit
AU_LIBS   := -framework AudioToolbox -framework AudioUnit -framework CoreAudio -framework CoreFoundation -framework Cocoa
ifneq (,$(EXTRA))
all: $(EXTRA)
endif

au: $(AU_BIN) $(COMPONENT)/Contents/Info.plist $(addprefix $(COMPONENT)/Contents/Resources/,$(DOCS))

$(AU_BIN): src/au/au.m src/ui/*.h $(DSP_SRC)
	mkdir -p $(dir $@)
	$(CC) $(COMMON) -x objective-c -fno-objc-arc $(CFLAGS) $< -o $@ -bundle $(LDFLAGS) $(AU_LIBS) -lm

$(COMPONENT)/Contents/Info.plist: bundle/au/Info.plist
	mkdir -p $(dir $@)
	cp $< $@

$(COMPONENT)/Contents/Resources/%: %
	mkdir -p $(dir $@)
	cp $< $@

$(BUNDLE) build-test $(DISTDIR):
	mkdir -p $@

$(PLUGIN): $(DSP_SRC) | $(BUNDLE)
	$(CC) $(COMMON) $(CSTD) $(CFLAGS) $< -o $@ $(SHARED) $(LDFLAGS) -lm

$(UI): src/ui/ui_lv2.c src/ui/*.h src/ports.h src/lv2_abi.h | $(BUNDLE)
	$(CC) $(COMMON) $(UI_LANG) $(CFLAGS) $< -o $@ $(SHARED) $(LDFLAGS) $(UI_LIBS) -lm

$(BUNDLE)/manifest.ttl: bundle/manifest.ttl.in | $(BUNDLE)
	sed -e 's/@LIB_EXT@/$(LIB_EXT)/g' -e 's/@UI_CLASS@/$(UI_CLASS)/' $< > $@

$(BUNDLE)/tangerine_orbit.ttl: bundle/tangerine_orbit.ttl | $(BUNDLE)
	cp $< $@

# Licence, notices and the AI usage statement travel with the bundle.
$(BUNDLE)/%: % | $(BUNDLE)
	cp $< $@

# Offline tests (native architecture only, so they can run on the build machine).
# test_dsp also loads the built plugin library, which is universal on macOS.
NATIVE := $(OPT) $(WARN) -fno-math-errno $(CSTD)

test: all build-test/test_dsp build-test/ui_render
	./build-test/test_dsp $(BUNDLE)
	./build-test/ui_render build-test/panel.bmp

build-test/test_dsp: test/test_dsp.c $(DSP_SRC) | build-test
	$(CC) $(NATIVE) $< -o $@ $(TEST_LIBS) -lm

build-test/ui_render: test/ui_render.c src/ui/*.h | build-test
	$(CC) $(NATIVE) $< -o $@ -lm

install: all
	mkdir -p "$(LV2DIR)"
	rm -rf "$(LV2DIR)/tangerine_orbit.lv2"
	cp -R $(BUNDLE) "$(LV2DIR)/"
ifneq (,$(EXTRA))
	mkdir -p "$(AUDIR)"
	rm -rf "$(AUDIR)/tangerine_orbit.component"
	cp -R $(COMPONENT) "$(AUDIR)/"
endif

dist: all | $(DISTDIR)
	rm -rf $(DISTDIR)/$(DISTNAME) $(DISTDIR)/$(DISTNAME).zip
	mkdir -p $(DISTDIR)/$(DISTNAME)
	cp -R $(BUNDLE) $(if $(EXTRA),$(COMPONENT)) README.md $(DOCS) $(DISTDIR)/$(DISTNAME)/
	cd $(DISTDIR) && zip -qr $(DISTNAME).zip $(DISTNAME)
	@echo "Packaged $(DISTDIR)/$(DISTNAME).zip"

clean:
	rm -rf build build-test

.PHONY: all au test install dist clean

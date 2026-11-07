# SPDX-License-Identifier: GPL-3.0-or-later
# PS5 Thermal Doctor - build with ps5-payload-sdk (Linux / WSL / GitHub Actions)
# PS5 Thermal Doctor - ps5-payload-sdk ile derleme (Linux / WSL / GitHub Actions)
#
#   export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
#   make                      -> ps5-thermal-doctor.elf
#   make deploy PS5_HOST=<ip> -> build and send to elfldr (port 9021)
#   make test                 -> PC tests (gcc)
#   make clean
#
# Windows without WSL: tools/build.ps1 (docs/INSTALL.md)

PS5_PAYLOAD_SDK ?= /opt/ps5-payload-sdk
PS5_HOST ?= ps5
PS5_PORT ?= 9021
HOST_CC  ?= cc

PORTABLE := src/analysis.c src/history.c src/config.c src/util.c src/i18n.c src/log.c

ifneq ($(MAKECMDGOALS),test)
ifeq ($(wildcard $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk),)
$(error PS5_PAYLOAD_SDK not found / bulunamadı: $(PS5_PAYLOAD_SDK) - see docs/INSTALL.md)
endif
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
endif

PYTHON ?= python3
ELF    := ps5-thermal-doctor.elf

SRCS := src/main.c src/platform.c src/probe.c src/sampler.c src/http.c src/tile.c $(PORTABLE)
GEN  := gen/web_index.c gen/icon0.c
HDRS := $(wildcard src/*.h)

CFLAGS  := -Os -std=gnu11 -Wall -Wextra -Isrc -ffunction-sections -fdata-sections \
           -fno-asynchronous-unwind-tables -fno-unwind-tables
LDFLAGS := -Wl,--gc-sections
# kernel_sys: temperature/fan/power; SceSystemService: running game, system language;
# SceAppInstUtil: home-screen tile; pthread: threads
LDADD   := -lkernel_sys -lkernel -lSceSystemService -lSceAppInstUtil -lpthread

all: $(ELF)

gen:
	mkdir -p gen

gen/web_index.c: web/index.html tools/gen_assets.py | gen
	$(PYTHON) tools/gen_assets.py web/index.html $@ web_index_html

gen/icon0.c: assets/icon0.png tools/gen_assets.py | gen
	$(PYTHON) tools/gen_assets.py assets/icon0.png $@ icon0_png

$(ELF): $(SRCS) $(GEN) $(HDRS) Makefile
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRCS) $(GEN) $(LDADD)
	-"$(PS5_PAYLOAD_SDK)/bin/prospero-strip" --strip-all $@
	@ls -l $@

deploy: $(ELF)
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $(ELF)

test:
	mkdir -p build
	$(HOST_CC) -std=gnu11 -Wall -Wextra -O1 -pthread -Isrc -o build/test_analysis $(PORTABLE) test/test_analysis.c -lm
	./build/test_analysis

clean:
	rm -rf $(ELF) gen build

.PHONY: all deploy test clean

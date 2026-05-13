#===============================================================================
# 3DS Audio Player — Makefile
#
# Targets:
#   make          → audioplayer.3dsx   (run from Homebrew Launcher)
#   make cia      → audioplayer.cia    (installable title, 96MB on old 3DS)
#   make tools    → download makerom + bannertool into tools/
#   make banner   → auto-generate banner/banner.png + banner/banner.wav
#   make clean    → remove build outputs
#   make distclean→ clean + remove downloaded tools and banner/
#
# Prerequisites (installed via devkitPro pacman):
#   pacman -S devkitARM libctru citro2d 3dstools
#
# CIA-only extras (auto-downloaded by `make tools` if not in PATH):
#   makerom    https://github.com/3DSGuy/Project_CTR
#   bannertool https://github.com/Steveice10/bannertool
#===============================================================================

APP_TITLE    := 3DS Audio Player
APP_AUTHOR   := You
APP_DESC     := MP3/OGG/FLAC/WAV player with pitch and speed control
APP_VERSION  := 1.0.0

# Unique title ID for the CIA.  Change this if you distribute publicly.
# Format: 00040000 + 8 hex digits.  Avoid IDs used by real 3DS titles.
TITLE_ID     := 0004000000BEEF00

TARGET       := audioplayer
BUILD        := build
SOURCES      := source
ROMFS        := romfs
TOOLS_DIR    := $(CURDIR)/tools
BANNER_DIR   := $(CURDIR)/banner

#===============================================================================
# devkitPro sanity checks
#===============================================================================
ifeq ($(strip $(DEVKITPRO)),)
  $(error DEVKITPRO is not set. Source /etc/profile.d/devkit-env.sh or run: export DEVKITPRO=/opt/devkitpro)
endif
ifeq ($(strip $(DEVKITARM)),)
  $(error DEVKITARM is not set. export DEVKITARM=/opt/devkitpro/devkitARM)
endif

CTRULIB := $(DEVKITPRO)/libctru

include $(DEVKITARM)/3ds_rules

#===============================================================================
# Host OS detection — selects the correct prebuilt binary to download
#===============================================================================
UNAME := $(shell uname -s)
ifeq ($(UNAME),Linux)
  HOST_OS    := linux
  EXE        :=
else ifeq ($(UNAME),Darwin)
  HOST_OS    := macos
  EXE        :=
else
  HOST_OS    := windows
  EXE        := .exe
endif

#===============================================================================
# Tool detection
#
# Priority: PATH → devkitPro tools/bin → local tools/ dir
# `make tools` downloads into tools/ if nothing else is found.
#===============================================================================
_MAKEROM_BIN := $(shell command -v makerom$(EXE) 2>/dev/null)
ifeq ($(_MAKEROM_BIN),)
  _MAKEROM_BIN := $(wildcard $(DEVKITPRO)/tools/bin/makerom$(EXE))
endif
ifeq ($(_MAKEROM_BIN),)
  _MAKEROM_BIN := $(TOOLS_DIR)/makerom$(EXE)
endif
MAKEROM := $(_MAKEROM_BIN)

_BANNERTOOL_BIN := $(shell command -v bannertool$(EXE) 2>/dev/null)
ifeq ($(_BANNERTOOL_BIN),)
  _BANNERTOOL_BIN := $(wildcard $(DEVKITPRO)/tools/bin/bannertool$(EXE))
endif
ifeq ($(_BANNERTOOL_BIN),)
  _BANNERTOOL_BIN := $(TOOLS_DIR)/bannertool$(EXE)
endif
BANNERTOOL := $(_BANNERTOOL_BIN)

#===============================================================================
# Download URLs — pinned to known-good releases
#
#   makerom    v0.18.3  by 3DSGuy  (Project_CTR)
#   bannertool 1.2.0    by Steveice10
#===============================================================================
MAKEROM_VER     := v0.18.3
BANNERTOOL_VER  := 1.2.0

MAKEROM_BASE    := https://github.com/3DSGuy/Project_CTR/releases/download/makerom-$(MAKEROM_VER)
BANNERTOOL_BASE := https://github.com/Steveice10/bannertool/releases/download/$(BANNERTOOL_VER)

ifeq ($(HOST_OS),linux)
  MAKEROM_ZIP         := makerom-$(MAKEROM_VER)-ubuntu_x86_64.zip
  BANNERTOOL_ZIP      := bannertool.zip
  BANNERTOOL_BIN_PATH := linux_x86_64/bannertool
else ifeq ($(HOST_OS),macos)
  MAKEROM_ZIP         := makerom-$(MAKEROM_VER)-macos_x86_64.zip
  BANNERTOOL_ZIP      := bannertool.zip
  BANNERTOOL_BIN_PATH := macos_x86_64/bannertool
else
  MAKEROM_ZIP         := makerom-$(MAKEROM_VER)-win_x86_64.zip
  BANNERTOOL_ZIP      := bannertool.zip
  BANNERTOOL_BIN_PATH := windows_x86_64/bannertool.exe
endif

#===============================================================================
# Compiler / linker flags
#===============================================================================
INCLUDE  := -I$(SOURCES) \
            -Ivendor \
            -I$(CTRULIB)/include \
            -I$(DEVKITPRO)/portlibs/3ds/include

LIBDIRS  := $(CTRULIB) $(DEVKITPRO)/portlibs/3ds
LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ARCH     := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS   := -g -Wall -O2 -mword-relocations -ffunction-sections \
            $(ARCH) $(INCLUDE) -D__3DS__
CXXFLAGS := $(CFLAGS) -std=c++17
ASFLAGS  := -g $(ARCH)
LDFLAGS  := -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS     := -lcitro2d -lcitro3d -lctru -lm

#===============================================================================
# Source → object file lists
#===============================================================================
CFILES  := $(wildcard $(SOURCES)/*.c)
OFILES  := $(patsubst $(SOURCES)/%.c,$(BUILD)/%.o,$(CFILES))
OUTPUT  := $(CURDIR)/$(TARGET)

#===============================================================================
# Default target: .3dsx
#===============================================================================
.PHONY: all cia tools banner _check_tools _ensure_banner clean distclean

all: $(BUILD) $(OUTPUT).3dsx
	@echo ""
	@echo "  Built: $(OUTPUT).3dsx"
	@echo "  Copy to /3ds/$(TARGET)/ on your SD card."

#===============================================================================
# CIA target
#===============================================================================
cia: $(BUILD) $(OUTPUT).elf $(OUTPUT).smdh _check_tools _ensure_banner
	@echo "  [BNR] $(OUTPUT).bnr"
	$(BANNERTOOL) makebanner \
	    -i $(BANNER_DIR)/banner.png \
	    -a $(BANNER_DIR)/banner.wav \
	    -o $(OUTPUT).bnr
	@echo "  [CIA] $(OUTPUT).cia"
	$(MAKEROM) -f cia \
	    -o  $(OUTPUT).cia \
	    -elf $(OUTPUT).elf \
	    -rsf audioplayer.rsf \
	    -icon $(OUTPUT).smdh \
	    -banner $(OUTPUT).bnr \
	    -exefslogo
	@echo ""
	@echo "  Built: $(OUTPUT).cia"
	@echo "  Install via FBI: copy .cia to SD card → FBI → SD → ..."
	@echo "  Requires Luma3DS for 96MB extended memory on old 3DS."

# Abort with a clear message if a required CIA tool is missing
_check_tools:
	@if [ ! -f "$(MAKEROM)" ]; then \
	    echo ""; \
	    echo "  ERROR: makerom not found (looked in PATH, devkitPro, $(TOOLS_DIR))"; \
	    echo "  Fix:  make tools"; \
	    echo "  Or:   dkp-pacman -S makerom   (devkitPro Windows/WSL package)"; \
	    echo ""; \
	    exit 1; \
	fi
	@if [ ! -f "$(BANNERTOOL)" ]; then \
	    echo ""; \
	    echo "  ERROR: bannertool not found (looked in PATH, devkitPro, $(TOOLS_DIR))"; \
	    echo "  Fix:  make tools"; \
	    echo ""; \
	    exit 1; \
	fi

# Auto-generate banner assets if they don't exist yet
_ensure_banner: $(TOOLS_DIR)/gen_banner.py
	@mkdir -p $(BANNER_DIR)
	@if [ ! -f "$(BANNER_DIR)/banner.png" ]; then \
	    echo "  [GEN] banner/banner.png  (auto-generated — replace with your own art)"; \
	    python3 $(TOOLS_DIR)/gen_banner.py "$(BANNER_DIR)/banner.png"; \
	fi
	@if [ ! -f "$(BANNER_DIR)/banner.wav" ]; then \
	    echo "  [GEN] banner/banner.wav  (0.5s silence — replace with your own audio)"; \
	    python3 -c "\
import wave, array; \
w = wave.open('$(BANNER_DIR)/banner.wav', 'w'); \
w.setnchannels(2); w.setsampwidth(2); w.setframerate(44100); \
w.writeframes(array.array('h', [0]*44100).tobytes()); \
w.close()"; \
	fi

#===============================================================================
# `make tools` — download makerom and bannertool prebuilt binaries
#               into tools/ and write the banner generator script
#===============================================================================
tools: $(TOOLS_DIR)/makerom$(EXE) \
       $(TOOLS_DIR)/bannertool$(EXE) \
       $(TOOLS_DIR)/gen_banner.py
	@echo ""
	@echo "  All tools ready in $(TOOLS_DIR)/"
	@echo "  makerom:    $(TOOLS_DIR)/makerom$(EXE)"
	@echo "  bannertool: $(TOOLS_DIR)/bannertool$(EXE)"

$(TOOLS_DIR)/makerom$(EXE):
	@echo "  [DL] makerom $(MAKEROM_VER) for $(HOST_OS)..."
	@mkdir -p $(TOOLS_DIR)
	@if command -v curl >/dev/null 2>&1; then \
	    curl -fSL -o "$(TOOLS_DIR)/makerom.zip" \
	        "$(MAKEROM_BASE)/$(MAKEROM_ZIP)"; \
	else \
	    wget -q -O "$(TOOLS_DIR)/makerom.zip" \
	        "$(MAKEROM_BASE)/$(MAKEROM_ZIP)"; \
	fi
	@cd "$(TOOLS_DIR)" && \
	    unzip -o makerom.zip "makerom$(EXE)" 2>/dev/null || \
	    unzip -o makerom.zip -d _tmp && \
	    find _tmp -name "makerom$(EXE)" -exec cp {} . \; && \
	    rm -rf _tmp
	@chmod +x "$(TOOLS_DIR)/makerom$(EXE)"
	@rm -f "$(TOOLS_DIR)/makerom.zip"
	@echo "  makerom ready."

$(TOOLS_DIR)/bannertool$(EXE):
	@echo "  [DL] bannertool $(BANNERTOOL_VER) for $(HOST_OS)..."
	@mkdir -p $(TOOLS_DIR)
	@if command -v curl >/dev/null 2>&1; then \
	    curl -fSL -o "$(TOOLS_DIR)/bannertool.zip" \
	        "$(BANNERTOOL_BASE)/$(BANNERTOOL_ZIP)"; \
	else \
	    wget -q -O "$(TOOLS_DIR)/bannertool.zip" \
	        "$(BANNERTOOL_BASE)/$(BANNERTOOL_ZIP)"; \
	fi
	@mkdir -p "$(TOOLS_DIR)/_bttmp"
	@cd "$(TOOLS_DIR)/_bttmp" && unzip -o "../bannertool.zip"
	@cp "$(TOOLS_DIR)/_bttmp/$(BANNERTOOL_BIN_PATH)" \
	    "$(TOOLS_DIR)/bannertool$(EXE)"
	@rm -rf "$(TOOLS_DIR)/_bttmp" "$(TOOLS_DIR)/bannertool.zip"
	@chmod +x "$(TOOLS_DIR)/bannertool$(EXE)"
	@echo "  bannertool ready."

# Pure-Python 256x128 PNG generator — no Pillow/ImageMagick required.
# Writes a purple-gradient banner matching the app colour scheme.
$(TOOLS_DIR)/gen_banner.py:
	@mkdir -p $(TOOLS_DIR)
	@printf '%s\n' \
'#!/usr/bin/env python3' \
'"""' \
'gen_banner.py <output.png>' \
'Generates a 256x128 Home Menu banner PNG using only stdlib (zlib, struct).' \
'Replace banner/banner.png with your own 256x128 art at any time.' \
'"""' \
'import sys, zlib, struct' \
'W, H = 256, 128' \
'def chunk(tag, data):' \
'    crc = zlib.crc32(tag + data) & 0xFFFFFFFF' \
'    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)' \
'def write_png(path, rows):' \
'    raw = b""' \
'    for row in rows:' \
'        raw += b"\x00" + b"".join(bytes(p) for p in row)' \
'    with open(path, "wb") as f:' \
'        f.write(b"\x89PNG\r\n\x1a\n")' \
'        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)))' \
'        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))' \
'        f.write(chunk(b"IEND", b""))' \
'FONT = {' \
' " ":[0,0,0,0,0],' \
' "3":[0x22,0x49,0x49,0x49,0x36],' \
' "D":[0x7F,0x41,0x41,0x22,0x1C],' \
' "S":[0x32,0x49,0x49,0x49,0x26],' \
' "A":[0x3E,0x09,0x09,0x09,0x3E],' \
' "u":[0x3C,0x40,0x40,0x20,0x7C],' \
' "d":[0x38,0x44,0x44,0x28,0x7C],' \
' "i":[0,0x44,0x7D,0x40,0],' \
' "o":[0x38,0x44,0x44,0x44,0x38],' \
' "P":[0x7F,0x09,0x09,0x09,0x06],' \
' "l":[0,0x41,0x7F,0x40,0],' \
' "y":[0x4C,0x50,0x50,0x50,0x3C],' \
' "e":[0x38,0x54,0x54,0x54,0x18],' \
' "r":[0x7C,0x08,0x04,0x04,0x08],' \
' "n":[0x7C,0x04,0x04,0x78,0],' \
'}' \
'def draw_str(px, text, x, y, col):' \
'    for ch in text:' \
'        for ci, b in enumerate(FONT.get(ch, [0]*5)):' \
'            for ri in range(7):' \
'                if b & (1 << ri):' \
'                    xi, yi = x+ci, y+ri' \
'                    if 0<=xi<W and 0<=yi<H: px[yi][xi] = col' \
'        x += 6' \
'BG1=(0x12,0x12,0x1E); BG2=(0x1E,0x18,0x35); ACC=(0x7C,0x3A,0xFF); WHITE=(255,255,255)' \
'px = [[(int(BG1[c]+(BG2[c]-BG1[c])*r/(H-1)),)*1 and' \
'       (int(BG1[0]+(BG2[0]-BG1[0])*r/(H-1)),' \
'        int(BG1[1]+(BG2[1]-BG1[1])*r/(H-1)),' \
'        int(BG1[2]+(BG2[2]-BG1[2])*r/(H-1)))' \
'       for _ in range(W)] for r in range(H)]' \
'for r in range(20):' \
'    t=r/19' \
'    for c in range(W): px[r][c]=(int(ACC[0]*(1-t*0.3)),int(ACC[1]*(1-t*0.3)),int(ACC[2]*(1-t*0.1)))' \
'for c in range(W): px[20][c]=(0x55,0x22,0xCC)' \
'draw_str(px,"3DS Audio Player",10,6,WHITE)' \
'write_png(sys.argv[1], px)' \
'print(f"Wrote {sys.argv[1]}")' \
	> $(TOOLS_DIR)/gen_banner.py
	@chmod +x $(TOOLS_DIR)/gen_banner.py

#===============================================================================
# `make banner` — (re)generate banner assets standalone
#===============================================================================
banner: $(TOOLS_DIR)/gen_banner.py
	@mkdir -p $(BANNER_DIR)
	@echo "  [GEN] banner/banner.png"
	@python3 $(TOOLS_DIR)/gen_banner.py "$(BANNER_DIR)/banner.png"
	@echo "  [GEN] banner/banner.wav  (0.5s stereo silence, 44100 Hz)"
	@python3 -c "\
import wave, array; \
w = wave.open('$(BANNER_DIR)/banner.wav', 'w'); \
w.setnchannels(2); w.setsampwidth(2); w.setframerate(44100); \
w.writeframes(array.array('h', [0]*44100).tobytes()); \
w.close()"
	@echo ""
	@echo "  Done.  Swap in your own 256x128 PNG and WAV then re-run 'make cia'."

#===============================================================================
# Core build rules
#===============================================================================
$(BUILD):
	@mkdir -p $@

$(BUILD)/%.o: $(SOURCES)/%.c
	@echo "  [CC]  $<"
	$(CC) $(CFLAGS) -c $< -o $@

$(OUTPUT).elf: $(OFILES)
	@echo "  [LD]  $(TARGET).elf"
	$(CC) $(LDFLAGS) $(OFILES) $(LIBPATHS) $(LIBS) -o $@

$(OUTPUT).smdh:
	@echo "  [SMDH] $(TARGET).smdh"
	smdhtool --create "$(APP_TITLE)" "$(APP_DESC)" "$(APP_AUTHOR)" \
	    $(CTRULIB)/default_icon.png $@

$(OUTPUT).3dsx: $(OUTPUT).elf $(OUTPUT).smdh
	@echo "  [3DSX] $(TARGET).3dsx"
	3dsxtool $< $@ --smdh=$(OUTPUT).smdh --romfs=$(ROMFS)

#===============================================================================
# Clean
#===============================================================================
clean:
	@echo "  Removing build outputs..."
	@rm -rf $(BUILD) \
	        $(OUTPUT).elf $(OUTPUT).3dsx $(OUTPUT).smdh \
	        $(OUTPUT).bnr $(OUTPUT).cia

distclean: clean
	@echo "  Removing downloaded tools and generated banner assets..."
	@rm -rf $(TOOLS_DIR) $(BANNER_DIR)

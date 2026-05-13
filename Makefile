#---------------------------------------------------------------------------------
# 3DS Audio Player — Makefile
# Requires devkitARM + libctru + citro2d
#---------------------------------------------------------------------------------

APP_TITLE    := 3DS Audio Player
APP_AUTHOR   := You
APP_DESC     := MP3/OGG/FLAC/WAV player with pitch and speed control
APP_VERSION  := 1.0.0

TARGET       := audioplayer
BUILD        := build
SOURCES      := source
ROMFS        := romfs

# devkitPro toolchain
ifeq ($(strip $(DEVKITPRO)),)
  $(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path>")
endif
ifeq ($(strip $(DEVKITARM)),)
  $(error "Please set DEVKITARM in your environment. export DEVKITARM=<path>")
endif

CTRULIB      := $(DEVKITPRO)/libctru

include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
# Includes and libs — defined BEFORE CFLAGS so they expand correctly
#---------------------------------------------------------------------------------
INCLUDE  := -I$(SOURCES) \
            -Ivendor \
            -I$(CTRULIB)/include \
            -I$(DEVKITPRO)/portlibs/3ds/include

LIBDIRS  := $(CTRULIB) $(DEVKITPRO)/portlibs/3ds
LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

#---------------------------------------------------------------------------------
# Compiler flags
#---------------------------------------------------------------------------------
ARCH     := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS   := -g -Wall -O2 -mword-relocations -ffunction-sections \
            $(ARCH) $(INCLUDE) -D__3DS__
CXXFLAGS := $(CFLAGS) -std=c++17
ASFLAGS  := -g $(ARCH)
LDFLAGS  := -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS     := -lcitro2d -lcitro3d -lctru -lm

#---------------------------------------------------------------------------------
# Source files — stb_vorbis.c lives in vendor/ so the wildcard never touches it;
# audio.c #includes it directly via -Ivendor.
#---------------------------------------------------------------------------------
CFILES   := $(wildcard $(SOURCES)/*.c)
OFILES   := $(patsubst $(SOURCES)/%.c, $(BUILD)/%.o, $(CFILES))

OUTPUT   := $(CURDIR)/$(TARGET)

#---------------------------------------------------------------------------------
# Targets
#---------------------------------------------------------------------------------
.PHONY: all clean

all: $(BUILD) $(OUTPUT).3dsx

$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: $(SOURCES)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OUTPUT).elf: $(OFILES)
	$(CC) $(LDFLAGS) $(OFILES) $(LIBPATHS) $(LIBS) -o $@

$(OUTPUT).smdh:
	smdhtool --create "$(APP_TITLE)" "$(APP_DESC)" "$(APP_AUTHOR)" \
	    $(CTRULIB)/default_icon.png $@

$(OUTPUT).3dsx: $(OUTPUT).elf $(OUTPUT).smdh
	3dsxtool $< $@ --smdh=$(OUTPUT).smdh --romfs=$(ROMFS)

clean:
	rm -rf $(BUILD) $(OUTPUT).elf $(OUTPUT).3dsx $(OUTPUT).smdh

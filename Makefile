#---------------------------------------------------------------------------------
# 3DS Audio Player — Makefile
# Requires devkitARM + libctru + citro2d + bannertool + makerom
#---------------------------------------------------------------------------------

APP_TITLE    := 3DS Audio Player
APP_AUTHOR   := You
APP_DESC     := MP3/OGG/FLAC/WAV player with pitch and speed control
APP_VERSION  := 1.0.0

TARGET       := audioplayer
BUILD        := build
SOURCES      := source
ROMFS        := romfs
RSF          := app.rsf

# devkitPro toolchain verification
ifeq ($(strip $(DEVKITPRO)),)
  $(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path>")
endif
ifeq ($(strip $(DEVKITARM)),)
  $(error "Please set DEVKITARM in your environment. export DEVKITARM=<path>")
endif

CTRULIB      := $(DEVKITPRO)/libctru

include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
# Includes and libs
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
# Source files
#---------------------------------------------------------------------------------
CFILES   := $(wildcard $(SOURCES)/*.c)
OFILES   := $(patsubst $(SOURCES)/%.c, $(BUILD)/%.o, $(CFILES))

OUTPUT   := $(CURDIR)/$(TARGET)

#---------------------------------------------------------------------------------
# Targets
#---------------------------------------------------------------------------------
.PHONY: all clean

all: $(BUILD) $(OUTPUT).3dsx $(OUTPUT).cia

$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: $(SOURCES)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OUTPUT).elf: $(OFILES)
	$(CC) $(LDFLAGS) $(OFILES) $(LIBPATHS) $(LIBS) -o $@

$(OUTPUT).smdh:
	smdhtool --create "$(APP_TITLE)" "$(APP_DESC)" "$(APP_AUTHOR)" \
	    $(CTRULIB)/default_icon.png $@

# Standard 3dsxtool compilation fixed (Removed broken --flags option)
$(OUTPUT).3dsx: $(OUTPUT).elf $(OUTPUT).smdh
	3dsxtool $< $@ --smdh=$(OUTPUT).smdh --romfs=$(ROMFS)

# Rules for building system asset configurations for the CIA file target
$(OUTPUT).bnr:
	bannertool makebanner -i $(CTRULIB)/default_icon.png -a $(CTRULIB)/default_icon.png -o $@

$(OUTPUT).icn:
	bannertool makesmdh -s "$(APP_TITLE)" -l "$(APP_DESC)" -p "$(APP_AUTHOR)" -i $(CTRULIB)/default_icon.png -o $@

# Final High Memory-Enabled CIA container generator
$(OUTPUT).cia: $(OUTPUT).elf $(OUTPUT).bnr $(OUTPUT).icn $(RSF)
	makerom -f cia -o $@ -rsf $(RSF) -elf $(OUTPUT).elf -banner $(OUTPUT).bnr -icon $(OUTPUT).icn -romfs $(ROMFS)

clean:
	rm -rf $(BUILD) $(OUTPUT).elf $(OUTPUT).3dsx $(OUTPUT).smdh $(OUTPUT).bnr $(OUTPUT).icn $(OUTPUT).cia

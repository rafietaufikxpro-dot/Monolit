# Monolit - build with devkitPro (devkitA64 + libnx).  Usage: make
.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error "Set DEVKITPRO first: export DEVKITPRO=/opt/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

TARGET      := Monolit
BUILD       := build
SOURCES     := source
DATA        :=
INCLUDES    := source

APP_TITLE   := Monolit
# Override from the command line: make APP_AUTHOR="your name"
APP_AUTHOR  ?= rafietaufik
APP_VERSION := 0.2.0

# Shown in-app (menu) and in the homebrew menu via the .nacp; single source of truth is APP_AUTHOR / APP_VERSION above.
DEFINES  := -DAPP_VERSION_STR='"$(APP_VERSION)"' -DAPP_AUTHOR_STR='"$(APP_AUTHOR)"'

ARCH     := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS   := -g -Wall -O2 -ffunction-sections $(ARCH) $(DEFINES)
CFLAGS   += $(INCLUDE) -D__SWITCH__
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++17
ASFLAGS  := -g $(ARCH)
LDFLAGS   = -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)
# SDL2 + SDL2_ttf from devkitPro portlibs: dkp-pacman -S switch-sdl2 switch-sdl2_ttf switch-pkg-config
PKGCONF  := $(DEVKITPRO)/portlibs/switch/bin/aarch64-none-elf-pkg-config
LIBS     := $(shell $(PKGCONF) --libs --static SDL2_ttf sdl2) -lnx
LIBDIRS  := $(PORTLIBS) $(LIBNX)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT  := $(CURDIR)/$(TARGET)
export TOPDIR  := $(CURDIR)
export VPATH   := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))

export LD      := $(CXX)
export OFILES  := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o)
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
export NROFLAGS += --nacp=$(CURDIR)/$(TARGET).nacp
# Optional app icon: put a 256x256 JPEG named icon.jpg next to this Makefile.
ifneq ($(wildcard $(CURDIR)/icon.jpg),)
export NROFLAGS += --icon=$(CURDIR)/icon.jpg
endif

.PHONY: $(BUILD) clean all test

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf

else

.PHONY: all
DEPENDS := $(OFILES:.o=.d)

all: $(OUTPUT).nro
$(OUTPUT).nro: $(OUTPUT).elf $(OUTPUT).nacp
# Rebuild the .nacp (title/author/version shown in the homebrew menu) whenever this Makefile changes.
$(OUTPUT).nacp: $(TOPDIR)/Makefile
$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif

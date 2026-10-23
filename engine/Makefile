#---------------------------------------------------------------------------------
# ttm3ds -- the To the Moon 3DS engine app.
#
# Standard devkitPro 3DS makefile plus mruby (with the engine's gems) and
# citro2d. Build with build.ps1, which sets DEVKITPRO / DEVKITARM and builds in a
# space-free directory. Output: ttm3ds.3dsx
#
# Needs the engine's libmruby.a first: build_mruby.ps1 puts it (and the matching
# headers) under $(MRUBY_BUILD)/3ds. romfs/ (the Ruby side) is packed in.
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

TARGET		:=	ttm3ds
BUILD		:=	build
SOURCES		:=	source
DATA		:=	data
INCLUDES	:=	include
ROMFS		:=	romfs

# Metadata that ends up in the SMDH (the HOME menu's icon + title text).
APP_TITLE := $(or $(TTM_APP_TITLE),To the Moon 3DS)
APP_DESCRIPTION := $(or $(TTM_APP_DESCRIPTION),To the Moon on the 3DS (needs your own copy of the game))
APP_ICON := $(if $(TTM_APP_ICON),$(TTM_APP_ICON),$(TOPDIR)/icon.png)
APP_AUTHOR := $(or $(TTM_APP_AUTHOR),ttm3ds port project)

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS	:=	-g -Wall -O2 -mword-relocations \
			-fomit-frame-pointer -ffunction-sections \
			$(ARCH)

# must match the defines libmruby.a was built with (build_config_3ds.rb)
CFLAGS	+=	$(INCLUDE) -D__3DS__ -DMRB_UTF8_STRING -DMRB_HIGH_PROFILE
CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11
ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

MRUBY_BUILD	?=	C:/Users/davao/devkitpro/build/engine

# mruby (with the engine gems) needs zlib from portlibs, citro2d/citro3d for graphics
LIBS	:=	-lmruby -lz -lcitro2d -lcitro3d -lctru -lm

# each dir contributes <dir>/include and <dir>/lib
LIBDIRS	:=	$(CTRULIB) $(PORTLIBS) $(MRUBY_BUILD)/3ds

#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

ifeq ($(strip $(CPPFILES)),)
	export LD	:=	$(CC)
else
	export LD	:=	$(CXX)
endif

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SRC	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES 	:=	$(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN	:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

#---------------------------------------------------------------------------------
# Embed romfs/ (the engine's Ruby side) into the 3DSX.
#---------------------------------------------------------------------------------
export _3DSXFLAGS += --romfs=$(CURDIR)/$(ROMFS)
ifneq ($(strip $(APP_ICON)),)
export _3DSXFLAGS += --smdh=$(CURDIR)/$(TARGET).smdh
endif

.PHONY: $(BUILD) clean all

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(OUTPUT).smdh $(TARGET).elf

else
.PHONY: all

DEPENDS	:=	$(OFILES:.o=.d)

all	:	$(OUTPUT).3dsx

# devkitPro's %.elf rule carries no prerequisites, so the objects must be wired
# up here or make goes straight to linking and ld reports "cannot find main.o".
$(OUTPUT).elf	:	$(OFILES)
$(OUTPUT).3dsx	:	$(OUTPUT).elf $(OUTPUT).smdh
$(OFILES_SRC)	:	$(HFILES_BIN)

%.bin.o	%_bin.h :	%.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

endif

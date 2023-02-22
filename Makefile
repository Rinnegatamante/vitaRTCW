TARGET		:= vitaRTCW
TITLE		:= RTCW00001
GIT_VERSION := $(shell git describe --abbrev=6 --dirty --always --tags)

SOURCES  := code/renderer code/qcommon code/botlib code/client code/server code/psp2 code/sys
CPPSOURCES := code/splines
INCLUDES := code/renderer code/qcommon code/botlib code/client code/server code/psp2 code/sys code/splines

LIBS = -lvitaGL -lvorbisfile -lvorbis -logg  -lspeexdsp -lmpg123 -lvitashark -lSceShaccCgExt \
	-lc -lSceCommonDialog_stub -lSceAudio_stub -lSceLibKernel_stub -ltaihen_stub \
	-lSceNet_stub -lSceNetCtl_stub -lpng -lz -lSceDisplay_stub -lSceGxm_stub \
	-lSceSysmodule_stub -lSceCtrl_stub -lSceTouch_stub -lSceMotion_stub -lm -lSceAppMgr_stub \
	-lSceAppUtil_stub -lScePgf_stub -ljpeg -lSceRtc_stub -lScePower_stub -lmathneon \
	-lSceShaccCg_stub -lSceKernelDmacMgr_stub

CFILES   := $(filter-out code/psp2/psp2_dll_hacks.c,$(foreach dir,$(SOURCES), $(wildcard $(dir)/*.c)))
CPPFILES   := $(foreach dir,$(CPPSOURCES), $(wildcard $(dir)/*.cpp))
BINFILES := $(foreach dir,$(DATA), $(wildcard $(dir)/*.bin))
OBJS     := $(addsuffix .o,$(BINFILES)) $(CFILES:.c=.o) $(CPPFILES:.cpp=.o)

export INCLUDE	:= $(foreach dir,$(INCLUDES),-I$(dir))

PREFIX  = arm-vita-eabi
CC      = $(PREFIX)-gcc
CXX      = $(PREFIX)-g++
CFLAGS  = $(INCLUDE) -D__PSP2__ -D__FLOAT_WORD_ORDER=1 -D__GNU__ \
        -DUSE_ICON -DARCH_STRING=\"arm\" -DBOTLIB -DUSE_CODEC_VORBIS \
        -DPRODUCT_VERSION=\"1.36_GIT_ba68b99c-2018-01-23\" -DHAVE_VM_COMPILED=true \
        -mfpu=neon -mcpu=cortex-a9 -fsigned-char -DRELEASE \
        -Wl,-q -O3 -g -ffast-math -fno-short-enums
CXXFLAGS  = $(CFLAGS) -fno-exceptions -std=gnu++11 -fpermissive
ASFLAGS = $(CFLAGS)

release:
	make -f Makefile.mp
	make clean
	make all

all: $(TARGET).vpk

$(TARGET).vpk: $(TARGET).velf 
	make -C code/cgame
	cp -f code/cgame/cgame.suprx ./cgame.sp.arm.suprx
	make -C code/ui
	cp -f code/ui/ui.suprx ./ui.sp.arm.suprx
	make -C code/game
	cp -f code/game/qagame.suprx ./qagame.sp.arm.suprx
	
	vita-make-fself -c -s $< build/eboot.bin
	vita-mksfoex -s TITLE_ID=$(TITLE) -d ATTRIBUTE2=12 "$(TARGET)" param.sfo
	cp -f param.sfo build/sce_sys/param.sfo

	#------------ Comment this if you don't have 7zip ------------------
	7z a -tzip ./$(TARGET).vpk -r ./build/sce_sys ./build/eboot.bin ./build/mp_eboot.bin
	#-------------------------------------------------------------------

eboot.bin: $(TARGET).velf
	vita-make-fself -c -s $< eboot.bin

%.velf: %.elf
	cp $< $<.unstripped.elf
	$(PREFIX)-strip -g $<
	vita-elf-create $< $@

$(TARGET).elf: $(OBJS)
	$(CXX) $(CXXFLAGS) $^ $(LIBS) -o $@

clean:
	@make -f Makefile.mp clean
	@make -C code/cgame clean
	@make -C code/ui clean
	@make -C code/game clean
	@rm -rf $(TARGET).velf $(TARGET).elf $(OBJS) $(TARGET).elf.unstripped.elf $(TARGET).vpk build/eboot.bin build/sce_sys/param.sfo ./param.sfo

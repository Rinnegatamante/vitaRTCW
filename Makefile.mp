TARGET		:= mp_eboot
GIT_VERSION := $(shell git describe --abbrev=6 --dirty --always --tags)

SOURCES  := mp_code/renderer mp_code/qcommon mp_code/botlib mp_code/client mp_code/server mp_code/psp2 mp_code/sys
CPPSOURCES := mp_code/splines
INCLUDES := mp_code/renderer mp_code/qcommon mp_code/botlib mp_code/client mp_code/server mp_code/psp2 mp_code/sys mp_code/splines

LIBS = -lvitaGL -lvorbisfile -lvorbis -logg  -lspeexdsp -lmpg123 \
	-lc -lSceCommonDialog_stub -lSceAudio_stub -lSceLibKernel_stub -lmathneon \
	-lSceNet_stub -lSceNetCtl_stub -lpng -lz -lSceDisplay_stub -lSceGxm_stub \
	-lSceSysmodule_stub -lSceCtrl_stub -lSceTouch_stub -lSceMotion_stub -lm -lSceAppMgr_stub \
	-lSceAppUtil_stub -lScePgf_stub -ljpeg -lSceRtc_stub -lScePower_stub -lcurl \
	-lssl -lcrypto -lSceSsl_stub -lvitashark -lSceShaccCg_stub

CFILES   := $(filter-out mp_code/psp2/psp2_dll_hacks.c,$(foreach dir,$(SOURCES), $(wildcard $(dir)/*.c)))
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
        -mfpu=neon -mcpu=cortex-a9 -fsigned-char -DRELEASE -DUSE_CURL \
        -Wl,-q -O2 -ftree-vectorize -g -ffast-math -fno-short-enums -DDEFAULT_BASEDIR=\"ux0:/data/iortcw\"
CXXFLAGS  = $(CFLAGS) -fno-exceptions -std=gnu++11 -fpermissive
ASFLAGS = $(CFLAGS)

all: $(TARGET).bin

$(TARGET).bin: $(TARGET).velf 
	make -C mp_code/cgame
	cp -f mp_code/cgame/cgame.suprx ./cgame.mp.arm.suprx
	make -C mp_code/ui
	cp -f mp_code/ui/ui.suprx ./ui.mp.arm.suprx
	make -C mp_code/game
	cp -f mp_code/game/qagame.suprx ./qagame.mp.arm.suprx
	
	vita-make-fself -c -s $< build/$(TARGET).bin

eboot.bin: $(TARGET).velf
	vita-make-fself -c -s $< eboot.bin

%.velf: %.elf
	cp $< $<.unstripped.elf
	$(PREFIX)-strip -g $<
	vita-elf-create $< $@

$(TARGET).elf: $(OBJS)
	$(CXX) $(CXXFLAGS) $^ $(LIBS) -o $@

clean:
	@make -C code/cgame clean
	@make -C code/ui clean
	@make -C code/game clean
	@rm -rf $(TARGET).velf $(TARGET).elf $(OBJS) $(TARGET).elf.unstripped.elf $(TARGET).vpk

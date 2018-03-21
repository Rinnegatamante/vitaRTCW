TARGET   := cgame
SOURCES  := .
CPPSOURCES := 
INCLUDES := ../renderer ../qcommon ../botlib ../client ../server ../psp2 ../sys ../splines

LIBS = -lvitaGL -lvorbisfile -lvorbis -logg  -lspeexdsp -lmpg123 \
	-lc -lSceCommonDialog_stub -lSceAudio_stub -lSceLibKernel_stub \
	-lSceNet_stub -lSceNetCtl_stub -lpng -lz -lSceDisplay_stub -lSceGxm_stub \
	-lSceSysmodule_stub -lSceCtrl_stub -lSceTouch_stub -lSceMotion_stub -lm -lSceAppMgr_stub \
	-lSceAppUtil_stub -lScePgf_stub -ljpeg -lSceRtc_stub -lScePower_stub	

CFILES   := $(foreach dir,$(SOURCES), $(wildcard $(dir)/*.c))
CPPFILES   := $(foreach dir,$(CPPSOURCES), $(wildcard $(dir)/*.cpp))
BINFILES := $(foreach dir,$(DATA), $(wildcard $(dir)/*.bin))
OBJS     := $(addsuffix .o,$(BINFILES)) $(CFILES:.c=.o) $(CPPFILES:.cpp=.o)

export INCLUDE	:= $(foreach dir,$(INCLUDES),-I$(dir))

PREFIX  = arm-vita-eabi
CC      = $(PREFIX)-gcc
CXX      = $(PREFIX)-g++
CFLAGS  = $(INCLUDE) -D__PSP2__ -D__FLOAT_WORD_ORDER=1 -D__GNU__ -DRELEASE \
        -DUSE_ICON -DARCH_STRING=\"arm\" -DCGAME -DUSE_CODEC_VORBIS \
        -DDEFAULT_BASEDIR=\"ux0:/data/iortcw\" -fno-lto \
        -DPRODUCT_VERSION=\"1.36_GIT_ba68b99c-2018-01-23\" -DHAVE_VM_COMPILED=true \
        -mfpu=neon -mcpu=cortex-a9 -fsigned-char \
        -Wl,-q -O3 -g -ffast-math -fno-short-enums
CXXFLAGS  = $(CFLAGS) -fno-exceptions -std=gnu++11 -fpermissive
ASFLAGS = $(CFLAGS)

all: $(TARGET).suprx

$(TARGET).suprx: $(TARGET).velf
	vita-make-fself -s $< $@

%.velf: %.elf
	cp $< $<.unstripped.elf
	$(PREFIX)-strip -g $<
	vita-elf-create -e $(<:.elf=.yml) $< $@

$(TARGET).elf: $(OBJS)
	$(CXX) $(CXXFLAGS) $^ $(LIBS) -o $@

clean:
	@rm -rf $(TARGET).velf $(TARGET).elf $(OBJS) $(TARGET).elf.unstripped.elf $(TARGET).suprx

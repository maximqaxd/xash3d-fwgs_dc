#
# Basic KallistiOS skeleton / test program
# Copyright (C)2001-2004 Megan Potter
#   

PROJECT_NAME = xash
TARGET = xash

include engine.mk

# Module paths and lib names
FILESYSTEM_DIR = filesystem
REF_GL_DIR = ref/gl
MAINUI_DIR = mainui_cpp
CL_DLL_DIR = ../hlsdk-portable_dc/cl_dll
SV_DLL_DIR = ../hlsdk-portable_dc/dlls

MAINUI_LIB = $(MAINUI_DIR)/libmenu.a
FILESYSTEM_LIB = $(FILESYSTEM_DIR)/libfilesystem_stdio.a

REF_GL_LIB = $(REF_GL_DIR)/libref_gl.a
CL_DLL_LIB = $(CL_DLL_DIR)/libcl_dll.a
SV_DLL_LIB = $(SV_DLL_DIR)/libhl.a

OBJS =  $(XASH_CLIENT_OBJS) $(XASH_OBJS) $(XASH_SERVER_OBJS) $(XASH_PLATFORM_OBJS)

LIBS = -L../hlsdk-portable_dc \
	   -L3rdparty/dreamcast/GLdc/build \
       -L$(KOS_BASE)/addons/lib/$(KOS_ARCH) \
       -L$(KOS_PORTS)/lib \
       -L$(FILESYSTEM_DIR) \
       -L$(REF_GL_DIR) \
       -L$(MAINUI_DIR) \
	   -lfatfs \
       -lfilesystem_stdio \
       -lhl \
	   -lcl_dll \
       -lref_gl \
       -l:libGL.a \
       -lppp
	   

# Build module libraries
$(CS_DLL_LIB):
	$(MAKE) -C $(CL_DLL_DIR)

$(FILESYSTEM_LIB):
	$(MAKE) -C $(FILESYSTEM_DIR)

$(REF_GL_LIB):
	$(MAKE) -C $(REF_GL_DIR)

$(SV_DLL_LIB):
	$(MAKE) -C $(SV_DLL_DIR)

# The rm-elf step is to remove the target before building, to force the
# re-creation of the rom disk.

all: $(FILESYSTEM_LIB) $(REF_GL_LIB) $(SV_DLL_LIB) $(TARGET) IP.BIN $(PROJECT_NAME).iso $(PROJECT_NAME).cdi

include $(KOS_BASE)/Makefile.rules

clean:
	-rm -f $(OBJS) 
	-rm -f $(TARGET)
	$(MAKE) -C $(FILESYSTEM_DIR) clean
	$(MAKE) -C $(REF_GL_DIR) clean
	-rm -f $(TARGET).bin
	-rm -f $(PROJECT_NAME).cdi
	-rm -f 1ST_READ.BIN
	-rm -f build/IP.BIN
	-rm -f $(PROJECT_NAME).iso
	-rm -f $(PROJECT_NAME).cdi
	

$(TARGET): $(OBJS) $(FILESYSTEM_LIB) $(REF_GL_LIB) $(SV_DLL_LIB) 
	kos-c++ -o $(TARGET) $(OBJS) $(LIBS)  -Wl,--gc-sections -fwhole-program -Wl,--build-id=none


run: $(TARGET)
	$(KOS_LOADER) $(TARGET)

1ST_READ_ISO.BIN: $(TARGET) IP.BIN
	kos-objcopy -R .stack -O binary $(TARGET) 1ST_READ.BIN 
	cp 1ST_READ.BIN build

1ST_READ.BIN: $(TARGET) IP.BIN
	kos-objcopy -R .stack -O binary $(TARGET) $(TARGET).BIN
	$(KOS_BASE)/utils/scramble/scramble $(TARGET).bin 1ST_READ.BIN
	-rm -f build/1ST_READ.BIN
	cp 1ST_READ.BIN build

IP.BIN: ip.txt
	-rm -f build/IP.BIN
	$(KOS_BASE)/utils/makeip/makeip ip.txt build/IP.BIN
	
$(PROJECT_NAME).iso: 1ST_READ_ISO.BIN
	mkisofs -V XashDC -G build/IP.BIN -r -J -l -o xash.iso build

$(PROJECT_NAME).cdi: 1ST_READ.BIN
	./mkdcdisc -e xash -D build -o Xash3D.cdi

.PHONY: all clean 1ST_READ.BIN IP.BIN cdi




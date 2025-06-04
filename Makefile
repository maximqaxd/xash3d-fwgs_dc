#
# Basic KallistiOS skeleton / test program
# Copyright (C)2001-2004 Megan Potter
#   

PROJECT_NAME = xash
TARGET = xash

include engine.mk

include 3rdparty/dreamcast/kos/Makefile.rules

# Module paths and lib names
FILESYSTEM_DIR = filesystem
REF_GL_DIR = ref/gl
MAINUI_DIR = libs/mainui_dc
CL_DLL_DIR = ../hlsdk-portable_dc/cl_dll
SV_DLL_DIR = ../hlsdk-portable_dc/dlls
UTILS_DIR = utils
KOS_DIR = 3rdparty/dreamcast/kos
GLDC_DIR = 3rdparty/dreamcast/GLdc


MAINUI_LIB = $(MAINUI_DIR)/libmenu.a
FILESYSTEM_LIB = $(FILESYSTEM_DIR)/libfilesystem_stdio.a
REF_GL_LIB = $(REF_GL_DIR)/libref_gl.a
CL_DLL_LIB = $(CL_DLL_DIR)/libcl_dll.a
SV_DLL_LIB = $(SV_DLL_DIR)/libhl.a

OBJS =  $(XASH_CLIENT_OBJS) $(XASH_OBJS) $(XASH_SERVER_OBJS) $(XASH_PLATFORM_OBJS)

LIBS = -L../hlsdk-portable_dc \
	   -L3rdparty/dreamcast/GLdc/dcbuild \
       -L$(KOS_BASE)/addons/lib/$(KOS_ARCH) \
       -L$(FILESYSTEM_DIR) \
       -L$(REF_GL_DIR) \
       -L$(MAINUI_DIR) \
       -lfilesystem_stdio \
       -lhl \
	   -lmenu\
	   -lcl_dll \
       -lref_gl \
       -l:libGL.a \
       -lppp \
	   -lpthread

# Step 1: Build all tools
tools: tools-qlumpy tools-pvrstudiomdl tools-pvrtex tools-makels tools-decompwad tools-mdldec

tools-qlumpy:
	@echo "Building qlumpy..."
	@$(MAKE) -C $(UTILS_DIR)/qlumpy

tools-pvrstudiomdl:
	@echo "Building pvrstudiomdl..."
	@$(MAKE) -C $(UTILS_DIR)/pvrstudiomdl

tools-pvrtex:
	@echo "Building pvrtex..."
	@$(MAKE) -C $(UTILS_DIR)/pvrtex

tools-makels:
	@echo "Building makels..."
	@$(MAKE) -C $(UTILS_DIR)/makels

tools-decompwad:
	@echo "Building decompwad..."
	@$(MAKE) -C $(UTILS_DIR)/decompwad

tools-mdldec:
	@echo "Building mdldec..."
	@$(MAKE) -C $(UTILS_DIR)/mdldec

build-kos:
	@echo "Building KOS..."
	@. $(KOS_DIR)/environ.sh && $(MAKE) -C $(KOS_DIR)

build-gldc:
	@echo "Building GLdc..."
	@mkdir -p $(GLDC_DIR)/dcbuild
	@cd $(GLDC_DIR)/dcbuild && cmake -DCMAKE_TOOLCHAIN_FILE=../toolchains/Dreamcast.cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release ..
	@cd $(GLDC_DIR)/dcbuild && make

# Step 2: Build engine and create IP.BIN
engine: build-kos build-gldc clean-public $(FILESYSTEM_LIB) $(REF_GL_LIB) $(SV_DLL_LIB) $(CL_DLL_LIB) $(TARGET) IP.BIN 1ST_READ.BIN

# Clean public folder object files
clean-public:
	@echo "Cleaning public folder object files..."
	-rm -f public/*.o

# Build module libraries
$(FILESYSTEM_LIB):
	$(MAKE) -C $(FILESYSTEM_DIR)

$(REF_GL_LIB):
	$(MAKE) -C $(REF_GL_DIR)

$(SV_DLL_LIB):
	$(MAKE) -C $(SV_DLL_DIR)

$(CL_DLL_LIB):
	$(MAKE) -C $(CL_DLL_DIR)

$(TARGET): $(OBJS) $(FILESYSTEM_LIB) $(REF_GL_LIB) $(SV_DLL_LIB) $(CL_DLL_LIB)
	kos-c++ -o $(TARGET) $(OBJS) $(LIBS) -Wl,--gc-sections -fwhole-program -Wl,--build-id=none -s

1ST_READ.BIN: $(TARGET) IP.BIN
	kos-objcopy -R .stack -O binary $(TARGET) $(TARGET).BIN
	$(KOS_BASE)/utils/scramble/scramble $(TARGET).bin 1ST_READ.BIN
	-rm -f build/1ST_READ.BIN
	cp 1ST_READ.BIN build

IP.BIN: ip.txt
	-rm -f build/IP.BIN
	$(KOS_BASE)/utils/makeip/makeip ip.txt build/IP.BIN

# Step 3: Repack game files
repack: clean-tools tools
	@echo "Repacking game files..."
	@$(MAKE) -f scripts/dreamcast/valve/repack_valve.mk all

# Step 4: Create CDI
cdi: engine repack
	@echo "Creating CDI image..."
	./mkdcdisc -e xash -D ../xash3d-hl_repack -p build/IP.BIN -N -o ../Xash3D_HL.cdi

# Main target that runs all steps in order
all: engine clean-tools tools repack cdi

# Clean targets
clean-tools:
	$(MAKE) -C $(UTILS_DIR)/qlumpy clean
	$(MAKE) -C $(UTILS_DIR)/pvrstudiomdl clean
	$(MAKE) -C $(UTILS_DIR)/pvrtex clean
	$(MAKE) -C $(UTILS_DIR)/makels clean
	$(MAKE) -C $(UTILS_DIR)/decompwad clean
	$(MAKE) -C $(UTILS_DIR)/mdldec clean

clean-engine:
	-rm -f $(OBJS) 
	-rm -f $(TARGET)
	$(MAKE) -C $(FILESYSTEM_DIR) clean
	$(MAKE) -C $(REF_GL_DIR) clean
	$(MAKE) -C $(SV_DLL_DIR) clean
	$(MAKE) -C $(CL_DLL_DIR) clean
	-rm -f $(TARGET).bin
	-rm -f 1ST_READ.BIN
	-rm -f build/IP.BIN

clean-repack:
	$(MAKE) -f scripts/dreamcast/valve/repack_valve.mk clean

clean: clean-tools clean-engine clean-repack
	-rm -f $(PROJECT_NAME).cdi
	-rm -f $(PROJECT_NAME).iso

.PHONY: all clean tools engine repack cdi clean-tools clean-engine clean-repack
.PHONY: tools-qlumpy tools-pvrstudiomdl tools-pvrtex tools-makels tools-decompwad tools-mdldec




# Graphics Repacking Makefile
# Handles gfx/env/*.bmp conversion and cleanup

# Use exported variables from repack_valve.mk
GFX_SRC_DIR = $(VALVE_DIR)/gfx
GFX_DST_DIR = $(REPACKED_DIR)/valve/gfx

# Find all BMP files in gfx/env
ENV_BMPS = $(wildcard $(GFX_SRC_DIR)/env/*.bmp)
ENV_PVRS = $(patsubst $(GFX_SRC_DIR)/env/%.bmp,$(GFX_DST_DIR)/env/%.pvr,$(ENV_BMPS))

# Default target
gfx-all: copy-gfx-files convert-env cleanup

# Copy required gfx files and directories
copy-gfx-files:
	@echo "Copying gfx files and directories..."
	@mkdir -p $(GFX_DST_DIR)
	# Copy shell directory if it exists
	@if [ -d "$(GFX_SRC_DIR)/shell" ]; then \
		cp -r $(GFX_SRC_DIR)/shell $(GFX_DST_DIR)/; \
	fi
	# Copy .lmp and .bmp files from main gfx directory
	@if [ -n "$$(find $(GFX_SRC_DIR) -maxdepth 1 -name '*.lmp' -o -name '*.bmp' 2>/dev/null)" ]; then \
		find $(GFX_SRC_DIR) -maxdepth 1 \( -name '*.lmp' -o -name '*.bmp' \) -exec cp {} $(GFX_DST_DIR)/ \; ; \
	fi

# Convert BMP files to PVR (directory should already exist from main setup)
convert-env: $(ENV_PVRS)

$(GFX_DST_DIR)/env/%.pvr: $(GFX_SRC_DIR)/env/%.bmp
	@echo "Converting $< to $@"
	@mkdir -p $(dir $@)
	$(PVRTEX) -i $< -o $@ -f RGB565 -c -r DOWN
	@rm -f $(GFX_DST_DIR)/env/$*.bmp

# Cleanup unwanted directories and files
cleanup:
	@echo "Cleaning up unwanted gfx directories and files..."
	@if [ -d "$(GFX_DST_DIR)/vgui" ]; then rm -rf $(GFX_DST_DIR)/vgui; fi
	@if [ -d "$(REPACKED_DIR)/valve/hw" ]; then rm -rf $(REPACKED_DIR)/valve/hw; fi
	@find $(GFX_DST_DIR) -name "*.tga" -delete 2>/dev/null || true

# Clean target
clean:
	@rm -rf $(GFX_DST_DIR)

.PHONY: gfx-all copy-gfx-files convert-env cleanup clean 
# General repacking Makefile for all repacking steps

ifndef VALVE_DIR
    MAKEFILE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
    VALVE_DIR := $(abspath $(MAKEFILE_DIR)../../../../Half-Life/valve)
    REPACKED_DIR := $(abspath $(MAKEFILE_DIR)../../../../xash3d-hl_repack)
    BUILD_DIR := $(abspath $(MAKEFILE_DIR)../../../build/valve)
    PVRTEX := $(abspath $(MAKEFILE_DIR)utils/pvrtex/pvrtex)
endif

export VALVE_DIR
export REPACKED_DIR
export BUILD_DIR
export PVRTEX

# Debug target to show paths
debug:
	@echo "Makefile directory: $(MAKEFILE_DIR)"
	@echo "Valve directory: $(VALVE_DIR)"
	@echo "Repacked directory: $(REPACKED_DIR)"
	@echo "Build directory: $(BUILD_DIR)"
	@echo "Current directory: $(CURDIR)"
	@echo "PVRTEX path: $(PVRTEX)"
	@echo "PVRTEX exists: $(shell test -f $(PVRTEX) && echo 'yes' || echo 'no')"

-include $(CURDIR)/scripts/dreamcast/valve/gfx.mk
-include $(CURDIR)/scripts/dreamcast/valve/wad.mk
-include $(CURDIR)/scripts/dreamcast/valve/models.mk
-include $(CURDIR)/scripts/dreamcast/valve/sounds.mk

copy_extra:
	@echo "Copying extra folders from $(VALVE_DIR) to $(REPACKED_DIR)/valve..."
	@mkdir -p $(REPACKED_DIR)/valve/logos
	@mkdir -p $(REPACKED_DIR)/valve/maps
	@mkdir -p $(REPACKED_DIR)/valve/overviews
	@mkdir -p $(REPACKED_DIR)/valve/resource
	@mkdir -p $(REPACKED_DIR)/valve/scripts
	@mkdir -p $(REPACKED_DIR)/valve/sprites
	@mkdir -p $(REPACKED_DIR)/valve/gfx/shell

	# Copy from VALVE_DIR
	@if [ -d "$(VALVE_DIR)/logos" ]; then cp -r $(VALVE_DIR)/logos/* $(REPACKED_DIR)/valve/logos/; fi
	@if [ -d "$(VALVE_DIR)/maps" ]; then cp -r $(VALVE_DIR)/maps/* $(REPACKED_DIR)/valve/maps/; fi
	@if [ -d "$(VALVE_DIR)/overviews" ]; then cp -r $(VALVE_DIR)/overviews/* $(REPACKED_DIR)/valve/overviews/; fi
	@if [ -d "$(VALVE_DIR)/scripts" ]; then cp -r $(VALVE_DIR)/scripts/* $(REPACKED_DIR)/valve/scripts/; fi
	@if [ -d "$(VALVE_DIR)/sprites" ]; then cp -r $(VALVE_DIR)/sprites/* $(REPACKED_DIR)/valve/sprites/; fi

	# Copy individual files from VALVE_DIR
	@for file in 800_textscheme.txt 1024_textscheme.txt 1152_textscheme.txt 1280_textscheme.txt 1600_textscheme.txt \
		cached.wad credits.txt default.cfg delta.lst fonts.wad gfx.wad language.cfg settings.scr skill.cfg \
		spectatormenu.txt spectcammenu.txt titles.txt user.scr valve.rc violence.cfg; do \
		if [ -f "$(VALVE_DIR)/$$file" ]; then \
			cp "$(VALVE_DIR)/$$file" "$(REPACKED_DIR)/valve/"; \
		fi \
	done

	# Copy from BUILD_DIR
	@if [ -f "$(BUILD_DIR)/resource/valve_english.txt" ]; then cp "$(BUILD_DIR)/resource/valve_english.txt" "$(REPACKED_DIR)/valve/resource/"; fi
	@if [ -f "$(BUILD_DIR)/resource/gameui_english.txt" ]; then cp "$(BUILD_DIR)/resource/gameui_english.txt" "$(REPACKED_DIR)/valve/resource/"; fi

	@if [ -d "$(BUILD_DIR)/gfx" ]; then cp -r $(BUILD_DIR)/gfx/* $(REPACKED_DIR)/valve/gfx; fi

	# Copy individual files from BUILD_DIR
	@for file in autoexec.cfg config.cfg creditsfont_cp1251.fnt dcjoy.cfg font2_cp1252.fnt game.ico \
		gameinfo.txt modem.cfg opengl.cfg video.cfg x360joy.cfg; do \
		if [ -f "$(BUILD_DIR)/$$file" ]; then \
			cp "$(BUILD_DIR)/$$file" "$(REPACKED_DIR)/valve/"; \
		fi \
	done

	# Copy root files from BUILD_DIR
	@if [ -f "$(BUILD_DIR)/1ST_READ.BIN" ]; then cp "$(BUILD_DIR)/1ST_READ.BIN" "$(REPACKED_DIR)/"; fi
	@if [ -f "$(BUILD_DIR)/IP.BIN" ]; then cp "$(BUILD_DIR)/IP.BIN" "$(REPACKED_DIR)/"; fi
	@if [ -f "$(BUILD_DIR)/0GDTEX.PVR" ]; then cp "$(BUILD_DIR)/0GDTEX.PVR" "$(REPACKED_DIR)/"; fi

	@echo "Extra files copied successfully."

# Main targets
gfx: gfx-all
wad: wad-all
models: models-all
sounds: sounds-convert-mp3 sounds-convert-sounds

all: gfx wad models sounds copy_extra
#all: copy_extra

# Global clean target that calls all specific clean targets
clean:
	@echo "Cleaning repacked files..."
	@rm -rf $(REPACKED_DIR)/valve
	@rm -f $(REPACKED_DIR)/1ST_READ.BIN
	@rm -f $(REPACKED_DIR)/IP.BIN
	@rm -f $(REPACKED_DIR)/0GDTEX.PVR

.PHONY: all gfx wad models sounds copy_extra clean debug

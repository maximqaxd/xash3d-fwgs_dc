# Sounds Repacking Makefile


ifndef VALVE_DIR
    MAKEFILE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
    VALVE_DIR := $(abspath $(MAKEFILE_DIR)../../../../Half-Life/valve)
    REPACKED_DIR := $(abspath $(MAKEFILE_DIR)../../../../xash3d-hl_repack)
endif

MEDIA_SRC_DIR := $(VALVE_DIR)/media
MEDIA_DST_DIR := $(REPACKED_DIR)/valve/media
SOUND_SRC_DIR := $(VALVE_DIR)/sound
SOUND_DST_DIR := $(REPACKED_DIR)/valve/sound
TEMP_SOUND_DIR = $(REPACKED_DIR)/temp_sound

FFMPEG = ffmpeg

MP3_FILES = $(shell find $(MEDIA_SRC_DIR) -name "*.mp3" 2>/dev/null)
WAV_FILES = $(patsubst $(MEDIA_SRC_DIR)/%.mp3,$(MEDIA_DST_DIR)/%.wav,$(MP3_FILES))

WAV_SRC_FILES = $(shell [ -d "$(SOUND_SRC_DIR)" ] && find $(SOUND_SRC_DIR) -type f -name "*.wav")
WAV_DST_FILES = $(patsubst $(SOUND_SRC_DIR)/%, $(SOUND_DST_DIR)/%, $(WAV_SRC_FILES))

# Debug target
sounds-debug:
	@echo "Makefile directory: $(MAKEFILE_DIR)"
	@echo "Valve directory: $(VALVE_DIR)"
	@echo "Repacked directory: $(REPACKED_DIR)"
	@echo "Sound source directory: $(SOUND_SRC_DIR)"
	@echo "Sound destination directory: $(SOUND_DST_DIR)"
	@echo "Found $(words $(WAV_SRC_FILES)) WAV files to convert"
	@if [ -n "$(WAV_SRC_FILES)" ]; then \
		echo "First few files to convert:"; \
		for f in $(wordlist 1,5,$(WAV_SRC_FILES)); do \
			echo "  $$f"; \
		done; \
	fi

# Setup dependencies
sounds-setup:
	@echo "Installing required tools..."
	@which ffmpeg >/dev/null 2>&1 || (echo "Installing ffmpeg..." && sudo apt-get update && sudo apt-get install -y ffmpeg)

# Default target
sounds-all: sounds-copy-media sounds-convert-mp3 sounds-convert-sounds sounds-cleanup

# Copy media directory structure (directory should already exist from main setup)
sounds-copy-media:
	@echo "Copying media directory structure..."
	@if [ -d "$(MEDIA_SRC_DIR)" ]; then \
		mkdir -p $(MEDIA_DST_DIR); \
		cp -r $(MEDIA_SRC_DIR)/* $(MEDIA_DST_DIR)/; \
	fi

# Convert MP3 files to ADPCM WAV
sounds-convert-mp3: $(WAV_FILES)

$(MEDIA_DST_DIR)/%.wav: $(MEDIA_SRC_DIR)/%.mp3
	@echo "Converting $< to $@"
	@mkdir -p $(dir $@)
	$(FFMPEG) -i "$<" -ac 1 -ar 11025 -f wav -acodec adpcm_yamaha "$@" -y

$(SOUND_DST_DIR)/%.wav: $(SOUND_SRC_DIR)/%.wav
	@echo "Converting $< to $@"
	@mkdir -p $(dir $@)
	$(FFMPEG) -i "$<" -ac 1 -ar 11025 -f wav -acodec adpcm_yamaha "$@" -y

sounds-convert-sounds: $(WAV_DST_FILES)
	cp $(VALVE_DIR)/sound/materials.txt $(REPACKED_DIR)/valve/sound/materials.txt
	cp $(VALVE_DIR)/sound/sentences.txt $(REPACKED_DIR)/valve/sound/sentences.txt
	@echo "Copied materials.txt and sentences.txt";

sounds-cleanup:
	@echo "Removing all MP3 files..."
	@find $(MEDIA_DST_DIR) -name "*.mp3" -delete 2>/dev/null || true

sounds-convert-mp3-fallback:
	@if [ -z "$(MP3_FILES)" ]; then \
		echo "No MP3 files found in $(MEDIA_SRC_DIR)"; \
	fi

sounds-clean:
	@rm -rf $(MEDIA_DST_DIR) $(SOUND_DST_DIR)

sounds: sounds-convert-mp3 sounds-convert-sounds

.PHONY: sounds-all sounds-copy-media sounds-convert-mp3 sounds-convert-sounds sounds-cleanup sounds-convert-mp3-fallback sounds-clean sounds-debug 
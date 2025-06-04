# Makefile for repacking WAD files

SHELL := /bin/bash

# Use exported variables from repack_valve.mk
WAD_SRC_DIR = $(VALVE_DIR)
WAD_TEMP_DIR = $(REPACKED_DIR)/temp_wad
WAD_DST_DIR = $(REPACKED_DIR)/valve

# Tools - use absolute paths
DECOMPWAD = $(CURDIR)/utils/decompwad/decompwad
PVRTEX = $(CURDIR)/utils/pvrtex/pvrtex
MAKELS = $(CURDIR)/utils/makels/makels
QLUMPY = $(CURDIR)/utils/qlumpy/qlumpy

# Find all .WAD files in the wad directory, excluding gfx.wad
WAD_FILES = $(filter-out $(WAD_SRC_DIR)/gfx.wad $(WAD_SRC_DIR)/cached.wad $(WAD_SRC_DIR)/fonts.wad, $(wildcard $(WAD_SRC_DIR)/*.wad))

wad-all: wad-decomp-wads wad-convert-bmps wad-makels wad-qlumpy

wad-decomp-wads: $(WAD_FILES)
	@echo "Decompil WAD files..."
	@mkdir -p $(WAD_TEMP_DIR)
	@set -x; for wad in $^; do \
		echo "Copying $$wad to $(WAD_TEMP_DIR)"; \
		cp $$wad $(WAD_TEMP_DIR); \
		echo "Running decompwad on $$(basename -- $$wad)"; \
		$(DECOMPWAD) $(WAD_TEMP_DIR)/$$(basename -- $$wad) $(WAD_TEMP_DIR); \
	done;

wad-convert-bmps:
	@echo "Converting BMP to PVR..."
	@find $(WAD_TEMP_DIR) -name "*.bmp" -o -name "*.BMP" | while read bmp_file; do \
		texname="$$(basename -- "$$bmp_file" .bmp)"; \
		texname="$$(basename -- "$$texname" .BMP)"; \
		width=$$(od -An -N4 -j18 -t u4 "$$bmp_file" | tr -d ' '); \
		height=$$(od -An -N4 -j22 -t u4 "$$bmp_file" | tr -d ' '); \
		if [[ "$$texname" == \{* ]]; then \
			format="ARGB1555"; \
		else \
			format="RGB565"; \
		fi; \
		pvr_file="$$(dirname "$$bmp_file")/$$texname.pvr"; \
		if [ ! -f "$$pvr_file" ]; then \
			if [ "$$width" -eq "$$height" ]; then \
				echo "Converting $$bmp_file $$width x $$height to $$pvr_file $$format"; \
				$(PVRTEX) -i "$$bmp_file" -o "$$pvr_file" -f $$format -c -m fast -r DOWN; \
			else \
				echo "Converting $$bmp_file $$width x $$height to $$pvr_file mipmap $$format"; \
				$(PVRTEX) -i "$$bmp_file" -o "$$pvr_file" -f $$format -c -r DOWN; \
			fi; \
		fi; \
	done;

wad-makels: $(WAD_FILES)
	@echo "Generating ls scripts for WAD files..."
	@set -x; for wad in $^; do \
		wadname="$$(basename -- $$wad .wad)"; \
		scriptname="$${wadname%%.*}.ls"; \
		sourcedir="$(WAD_TEMP_DIR)/$$wadname"; \
		mkdir -p $$sourcedir; \
		bmpdir="$$sourcedir/bmp"; \
		if [ -d "$$bmpdir" ]; then \
			echo "Copying PVR files from $$bmpdir to $$sourcedir"; \
			find $$bmpdir -name "*.pvr" -exec cp {} $$sourcedir \;; \
		fi; \
		echo "Generating ls script for $$wadname"; \
		$(MAKELS) $$sourcedir $$wadname $(WAD_TEMP_DIR)/$$scriptname; \
	done;

	@echo "Cleaning up original .wad files..."
	@rm -f $(WAD_TEMP_DIR)/*.wad

wad-qlumpy:
	@echo "Building WADs with qlumpy..."
	@set -x; for wad in $(WAD_FILES); do \
		wadname="$$(basename -- $$wad .wad)"; \
		scriptname="$${wadname%%.*}.ls"; \
		if [ -f "$(WAD_TEMP_DIR)/$$scriptname" ]; then \
			if [ "$$wadname" = "liquids" ]; then \
				echo "Renaming $$scriptname to liquids.ls"; \
				mv $(WAD_TEMP_DIR)/$$scriptname $(WAD_TEMP_DIR)/liquids.ls; \
				scriptname="liquids.ls"; \
			fi; \
			echo "Running qlumpy on $$scriptname"; \
			$(QLUMPY) $(WAD_TEMP_DIR)/$$scriptname; \
			echo "Moving $$wadname.wad to $(WAD_TEMP_DIR)"; \
			mv $$wadname.wad $(WAD_TEMP_DIR)/; \
			echo "Copying $$wadname.wad to $(WAD_DST_DIR)"; \
			cp $(WAD_TEMP_DIR)/$$wadname.wad $(WAD_DST_DIR); \
		fi; \
	done;

	@echo "Cleaning up temporary directories..."
	@rm -rf $(WAD_TEMP_DIR)

wad-clean:
	@rm -rf $(WAD_TEMP_DIR) $(WAD_DST_DIR)

wad-debug:
	@echo "WAD files to be processed:"
	@for wad in $(WAD_FILES); do echo "  $$wad"; done

.PHONY: wad-all wad-decomp-wads wad-convert-bmps wad-makels wad-qlumpy wad-clean wad-debug
# Models Repacking Makefile
# Decompiles MDL files and repacks textures to PVR format for Dreamcast

VALVE_DIR = ../Half-Life/valve
REPACKED_DIR = ../xash3d-hl_repack
TEMP_DIR = $(REPACKED_DIR)/temp_mdl
MODELS_SRC_DIR = $(VALVE_DIR)/models
MODELS_DST_DIR = $(REPACKED_DIR)/valve/models

SYS = $(shell $(CC) -dumpmachine)

ifneq (, $(findstring mingw, $(SYS)))
MDLDEC = utils/mdldec/mdldec.exe
PVRSTUDIOMDL = utils/pvrstudiomdl/pvrstudiomdl.exe
PVRTEX = utils/pvrtex/pvrtex.exe
else
MDLDEC = utils/mdldec/mdldec
PVRSTUDIOMDL = utils/pvrstudiomdl/pvrstudiomdl
PVRTEX = utils/pvrtex/pvrtex
endif

# Find all MDL files (excluding sequence group files ending with numbers and texture variants)
MDL_FILES = $(shell find $(MODELS_SRC_DIR) -name "*.mdl" 2>/dev/null | grep -v '[0-9][0-9]\.mdl$$' | \
	grep -v 'agruntt\.mdl$$' | grep -v 'apachet\.mdl$$' | grep -v 'barnaclet\.mdl$$' | \
	grep -v 'barneyt\.mdl$$' | grep -v 'big_momt\.mdl$$' | grep -v 'bullsquidt\.mdl$$' | \
	grep -v 'controllert\.mdl$$' | grep -v 'fungust\.mdl$$' | grep -v 'fungus.*t\.mdl$$' | \
	grep -v 'gargt\.mdl$$' | grep -v 'gmant\.mdl$$' | grep -v 'hairt\.mdl$$' | grep -v 'hassassint\.mdl$$' | grep -v 'headcrabt\.mdl$$' | \
	grep -v 'hgruntt\.mdl$$' | grep -v 'houndeyet\.mdl$$' | grep -v 'ickyt\.mdl$$' | \
	grep -v 'islavet\.mdl$$' | grep -v 'lightt\.mdl$$' | grep -v 'miniturrett\.mdl$$' | \
	grep -v 'ospreyt\.mdl$$' | grep -v 'playert\.mdl$$' | grep -v 'renginet\.mdl$$' | \
	grep -v 'scientistt\.mdl$$' | grep -v 'sentryt\.mdl$$' | grep -v 'tentacle2t\.mdl$$' | \
	grep -v 'treet\.mdl$$' | grep -v 'turrett\.mdl$$' | grep -v 'uplant1t\.mdl$$' | \
	grep -v 'uplant2t\.mdl$$' | grep -v 'uplant3t\.mdl$$' | grep -v 'w_9mmart\.mdl$$' | grep -v 'w_9mmarclipt\.mdl$$' | \
	grep -v 'w_9mmclipt\.mdl$$' | grep -v 'w_9mmhandgunt\.mdl$$' | grep -v 'w_357ammoboxt\.mdl$$' | \
	grep -v 'w_357ammot\.mdl$$' | grep -v 'w_357t\.mdl$$' | grep -v 'w_adrenalinet\.mdl$$' | \
	grep -v 'w_antidotet\.mdl$$' | grep -v 'w_argrenadet\.mdl$$' | grep -v 'w_batteryt\.mdl$$' | \
	grep -v 'w_chainammot\.mdl$$' | grep -v 'w_crossbowt\.mdl$$' | grep -v 'w_crowbart\.mdl$$' | \
	grep -v 'w_egont\.mdl$$' | grep -v 'w_flaret\.mdl$$' | grep -v 'w_gaussammot\.mdl$$' | \
	grep -v 'w_gausst\.mdl$$' | grep -v 'w_grenadet\.mdl$$' | grep -v 'w_hgunt\.mdl$$' | \
	grep -v 'w_isotopeboxt\.mdl$$' | grep -v 'w_longjumpt\.mdl$$' | grep -v 'w_medkitt\.mdl$$' | \
	grep -v 'w_oxygent\.mdl$$' | grep -v 'w_radt\.mdl$$' | grep -v 'w_rpgammot\.mdl$$' | \
	grep -v 'w_rpgt\.mdl$$' | grep -v 'w_satchelt\.mdl$$' | grep -v 'w_securityt\.mdl$$' | \
	grep -v 'w_shotboxt\.mdl$$' | grep -v 'w_shotgunt\.mdl$$' | grep -v 'w_shotshellt\.mdl$$' | \
	grep -v 'w_silencert\.mdl$$' | grep -v 'w_sqknestt\.mdl$$' | grep -v 'w_squeakt\.mdl$$' | \
	grep -v 'w_suitt\.mdl$$' | grep -v 'zombiet\.mdl$$' | grep -v 'doctor\.mdl$$')

# Generate relative paths for temp directory processing
TEMP_QC_FILES = $(patsubst $(MODELS_SRC_DIR)/%.mdl,$(TEMP_DIR)/%.qc,$(MDL_FILES))
FINAL_MDL_FILES = $(patsubst $(MODELS_SRC_DIR)/%.mdl,$(MODELS_DST_DIR)/%.mdl,$(MDL_FILES))

models-all: models-dirs models-copy-models models-decompile-models models-repack-models

models-dirs:
	@mkdir -p $(MODELS_DST_DIR)
	@mkdir -p $(TEMP_DIR)

models-copy-models:
	@echo "Copying models directory structure..."
	@if [ -d "$(MODELS_SRC_DIR)" ]; then \
		cp -r $(MODELS_SRC_DIR) $(TEMP_DIR)/; \
	fi

models-decompile-models: $(TEMP_QC_FILES)

$(TEMP_DIR)/%.qc: $(MODELS_SRC_DIR)/%.mdl | models-build-tools models-dirs models-copy-models
	@echo "Decompiling $< to $@"
	@mkdir -p $(dir $@)
	@model_base="$$(basename "$<" .mdl)"; \
	model_dir="$(MODELS_SRC_DIR)/$$(dirname "$*")"; \
	target_dir="$(dir $@)$$model_base"; \
	mkdir -p "$$target_dir"; \
	cp "$<" "$$target_dir/"; \
	texture_file="$$model_dir/$${model_base}T.mdl"; \
	if [ -f "$$texture_file" ]; then \
		cp "$$texture_file" "$$target_dir/"; \
	fi; \
	for seq_file in "$$model_dir/$${model_base}"[0-9][0-9].mdl; do \
		if [ -f "$$seq_file" ]; then \
			cp "$$seq_file" "$$target_dir/"; \
		fi; \
	done
	@model_base="$$(basename "$<" .mdl)"; \
	target_dir="$(dir $@)$$model_base"; \
	cd "$$target_dir" && MDLDEC_ACT_PATH="$(abspath utils/mdldec)" "$(abspath $(MDLDEC))" -a -t -m "$$(basename "$<")" "." || (echo "Decompilation failed for $<"; exit 1)
	@model_base="$$(basename "$<" .mdl)"; \
	target_dir="$(dir $@)$$model_base"; \
	rm -f "$$target_dir/$$(basename "$<")"; \
	if [ -f "$$target_dir/$${model_base}T.mdl" ]; then \
		rm -f "$$target_dir/$${model_base}T.mdl"; \
	fi; \
	for seq_file in "$$target_dir/$${model_base}"[0-9][0-9].mdl; do \
		if [ -f "$$seq_file" ]; then \
			rm -f "$$seq_file"; \
		fi; \
	done; \
	touch "$@"

models-repack-models: models-convert-textures models-compile-models

models-convert-textures:
	@if [ -d "$(TEMP_DIR)" ]; then \
		echo "Updating all SMD files to reference .PVR..."; \
		find $(TEMP_DIR) -name "*.smd" -exec sed -i 's/\.bmp\b/.pvr/Ig' {} \; ; \
		echo "Updating all QC files to reference .PVR..."; \
		find $(TEMP_DIR) -name "*.qc" -exec sed -i 's/\.bmp\b/.pvr/Ig' {} \; ; \
		echo "Fixing any accidental .pvr.pvr extensions in SMD and QC files..."; \
		find $(TEMP_DIR) -name "*.smd" -exec sed -i 's/\.pvr\.pvr\b/.pvr/Ig' {} \; ; \
		find $(TEMP_DIR) -name "*.qc"  -exec sed -i 's/\.pvr\.pvr\b/.pvr/Ig' {} \; ; \
		echo "Normalizing line endings in SMD and QC files..."; \
		find $(TEMP_DIR) -name "*.smd" -exec sed -i 's/\r$$//' {} \; ; \
		find $(TEMP_DIR) -name "*.qc"  -exec sed -i 's/\r$$//' {} \; ; \
		echo "Ensuring all SMD and QC files end with a newline..."; \
		find $(TEMP_DIR) -name "*.smd" -exec sh -c 'tail -c1 "$$1" | read -r _ || echo >> "$$1"' _ {} \; ; \
		find $(TEMP_DIR) -name "*.qc"  -exec sh -c 'tail -c1 "$$1" | read -r _ || echo >> "$$1"' _ {} \; ; \
		echo "Converting all BMP textures to PVR format..."; \
		find $(TEMP_DIR) -name "textures" -type d | while read texdir; do \
			echo "Processing texture directory: $$texdir"; \
			cd "$$texdir"; \
			bmp_count=0; \
			for bmp in *.bmp *.BMP; do \
				if [ -f "$$bmp" ]; then \
					bmp_count=$$((bmp_count + 1)); \
					texname="$$(basename "$$bmp" .bmp)"; \
					texname="$$(basename "$$texname" .BMP)"; \
					if [ ! -f "$${texname}.pvr" ]; then \
						echo "Converting $$bmp to $${texname}.pvr"; \
						"$(abspath $(PVRTEX))" -i "$$bmp" -o "$${texname}.pvr" -f RGB565 -c -r DOWN; \
					else \
						echo "$$texname.pvr already exists, skipping"; \
					fi; \
				fi; \
			done; \
			echo "Processed $$bmp_count BMP files in $$texdir"; \
			cd - > /dev/null; \
		done; \
		echo "Also converting any loose BMP in models directories..."; \
		find $(TEMP_DIR) -name "*.bmp" -o -name "*.BMP" | while read bmp_file; do \
			if [ -f "$$bmp_file" ]; then \
				bmp_dir="$$(dirname "$$bmp_file")"; \
				bmp_name="$$(basename "$$bmp_file")"; \
				texname="$$(basename "$$bmp_name" .bmp)"; \
				texname="$$(basename "$$texname" .BMP)"; \
				pvr_file="$${bmp_dir}/$${texname}.pvr"; \
				if [ ! -f "$$pvr_file" ]; then \
					echo "Converting $$bmp_file to $$pvr_file"; \
					"$(abspath $(PVRTEX))" -i "$$bmp_file" -o "$$pvr_file" -f RGB565 -c -r DOWN; \
				fi; \
			fi; \
		done; \
		echo "Renaming any *.bmp.pvr files to *.pvr..."; \
		find $(TEMP_DIR) -name '*.bmp.pvr' | while read bmp_pvr; do \
			base="$${bmp_pvr%.bmp.pvr}"; \
			mv "$$bmp_pvr" "$$base.pvr"; \
		done; \
		echo "Texture conversion and SMD updates completed"; \
	fi

models-compile-models:
	@echo "Compiling models with pvrstudiomdl..."
	@if [ -d "$(TEMP_DIR)" ]; then \
		find $(TEMP_DIR) -name "*.mdl" -delete; \
		find $(TEMP_DIR) -name "*.qc" -type f > /tmp/qc_files.txt; \
		qc_count=$$(wc -l < /tmp/qc_files.txt); \
		echo "Found $$qc_count QC files to process"; \
		processed=0; \
		failed=0; \
		while IFS= read -r qc && [ $$failed -eq 0 ]; do \
			if [ -f "$$qc" ]; then \
				processed=$$((processed + 1)); \
				echo "Processing $$processed/$$qc_count: $$qc..."; \
				qc_dir="$$(dirname "$$qc")"; \
				if ! ( \
					cd "$$qc_dir" && \
					echo "Compiling model with pvrstudiomdl..." && \
					qc_name="$$(basename "$$qc" .qc)" && \
					MDLDEC_ACT_PATH="$(abspath utils/mdldec)" "$(abspath $(PVRSTUDIOMDL))" "$$(basename "$$qc")" "$$qc_name" && \
					echo "Successfully compiled $$(basename "$$qc")" \
				); then \
					echo "Failed to process $$qc - stopping build"; \
					failed=1; \
				else \
					echo "Finished processing $$qc"; \
					echo "----------------------------------------"; \
				fi; \
			fi; \
		done < /tmp/qc_files.txt; \
		rm -f /tmp/qc_files.txt; \
		if [ $$failed -eq 1 ]; then \
			echo "Build failed due to compilation error"; \
			exit 1; \
		fi; \
		echo "Completed processing all $$processed models"; \
		echo "Copying all compiled MDL files to $(MODELS_DST_DIR)..."; \
		for mdl_file in $$(find $(TEMP_DIR) -mindepth 2 -name "*.mdl"); do \
			rel_path="$${mdl_file#$(TEMP_DIR)/}"; \
			if echo "$$rel_path" | grep -q '^player/player'; then \
				base_name="$$(basename "$$mdl_file")"; \
				dest_path="$(MODELS_DST_DIR)/$$base_name"; \
			elif echo "$$rel_path" | grep -q '^player/'; then \
				dest_path="$(MODELS_DST_DIR)/$$rel_path"; \
				dest_dir="$$(dirname "$$dest_path")"; \
				mkdir -p "$$dest_dir"; \
			else \
				base_name="$$(basename "$$mdl_file")"; \
				dest_path="$(MODELS_DST_DIR)/$$base_name"; \
			fi; \
			cp "$$mdl_file" "$$dest_path"; \
			echo "Copied $${rel_path} to $$dest_path"; \
		done; \
		rm -rf $(TEMP_DIR); \
		mdl_count=$$(find $(MODELS_DST_DIR) -name "*.mdl" | wc -l); \
		echo "Successfully copied $$mdl_count MDL files (including sequence files) to $(MODELS_DST_DIR)"; \
		echo "Removed temporary directory $(TEMP_DIR)"; \
	fi

models-clean-temp:
	@echo "Cleaning temporary files..."
	@rm -rf $(TEMP_DIR)

models-clean:
	@rm -rf $(REPACKED_DIR)

models-clean-tools:
	@$(MAKE) -C utils/mdldec clean
	@$(MAKE) -C utils/pvrstudiomdl clean

models-debug:
	@echo "MDL files to be processed:"
	@for mdl in $(MDL_FILES); do echo "  $$mdl"; done
	@echo ""
	@echo "QC files to be generated:"
	@for qc in $(TEMP_QC_FILES); do echo "  $$qc"; done

.PHONY: models-all models-build-tools models-dirs models-copy-models models-decompile-models models-repack-models models-convert-textures models-compile-models models-clean-temp models-clean models-clean-tools models-debug 
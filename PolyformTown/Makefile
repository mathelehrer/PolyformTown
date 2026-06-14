Q ?= @
CC = $(Q)gcc
CFLAGS = -O3 -Wall -Wextra -std=c11
SRC = src
BIN = bin
SPLASH ?= assets/splash/hat_town.ans
comma := ,

define announce
	@if [ -t 1 ]; then printf "\033[33m==> %s\033[0m\n" "$(1)"; else printf "==> %s\n" "$(1)"; fi
endef

CORE = $(SRC)/core/cycle.o $(SRC)/core/tile.o $(SRC)/core/hash.o \
    $(SRC)/core/vec.o $(SRC)/core/attach.o \
    $(SRC)/core/tetrille.o $(SRC)/core/lattice.o \
    $(SRC)/core/boundary.o $(SRC)/core/numerics.o \
    $(SRC)/core/term_color.o
POLY_SURFACE = $(SRC)/throughput/vcomp.o $(SRC)/throughput/poly_pipeline.o
VCOMP_SURFACE = $(SRC)/throughput/vcomp.o $(SRC)/throughput/vcomp_pipeline.o
RL0_SURFACE = $(SRC)/rl0/forget_map.o $(SRC)/rl0/boundary0.o $(SRC)/rl0/vcomp0.o $(SRC)/rl0/attach0.o

.PHONY: all tools data boot clean_boot_data boot_splash splash boot_rl0 boot_rl1 boot_rl2 boot_rl3 boot_rl4 boot_rl5 boot_rl6 rl6_print_conversion figures figures_files rl5_pathology_svg rl5_super6_sat_data rl5_overlap671213_sat_data rl5_sat_data rl5_super6_errata_placeholder rl5_super6_sat_check rl5_overlap671213_sat_check rl5_sat_solver_notice rl6_unified_data rl6_refine_data hce rephex rephex_print rephex_hacked rl7_inflation_figures \

.NOTPARALLEL: boot
	checks test clean \
	poly_count poly_print vcomp_count count_vcomp vcomp_print vcomp_parity \
	rl0_generate rl0_refine rl1_generate rl1_refine rl2_generate \
	rl3_generate rl4_generate rl4_refine \
	rl5_generate rl5_hexagons rl5_supertile_hexagons rl5_refine rl5_stats rl5_sat rl6_reduce rl6_refine rl6_generate hce rephex rephex_print supertile_depict pathological_item6 \
	imgtable tile_to_imgtable rl0_depict rl1_depict depict_search0_graph \
	rl0_svg rl0_refine_svg rl0_refine_svg_print rl1_svg_naive \
	rl1_survivors_svg rl2_survivors_svg rl3_survivors_svg \
	supertile_hex_models_svg supertile_svg overlap_supertile_svg \
	meta_graph tiling_3x3_demo tiling_4x4_demo hat_boundary_dfs_check \
	check_indexed_variants check_rl1_tile_order search1 refine1

# -----------------------------------------------------------------------------
# All
# -----------------------------------------------------------------------------
# A maximal default build surface
#

all: tools

tools: poly_count poly_print vcomp_count vcomp_print vcomp_parity \
	rl0_generate rl0_refine rl1_generate rl1_refine rl2_generate \
	rl3_generate rl4_generate rl4_refine \
	rl5_generate rl5_hexagons rl5_supertile_hexagons rl5_refine rl5_stats rl5_sat rl6_reduce rl6_refine rl6_generate hce rephex rephex_print supertile_depict pathological_item6 \
	imgtable tile_to_imgtable rl0_depict rl1_depict depict_search0_graph \
	rl1_generate_naive rl1_hexagons rl1_search \
	meta_graph tiling_3x3_demo tiling_4x4_demo \
	hat_boundary_dfs_check check_indexed_variants

data: rl0_data rl1_data rl1_data_naive meta_graph_data

$(SRC)/%.o: $(SRC)/%.c
	$(CC) $(CFLAGS) -I$(SRC) -c $< -o $@

$(BIN):
	@mkdir -p $(BIN)

clean:
	$(call announce,clean compiled artifacts)
	@rm -f $(SRC)/*.o $(SRC)/core/*.o $(SRC)/rl0/*.o \
		$(SRC)/rl1/*.o $(SRC)/rl2/*.o $(SRC)/rl3/*.o $(SRC)/rl4/*.o $(SRC)/rl5/*.o $(SRC)/rl6/*.o $(SRC)/rl7/*.o $(SRC)/rl1/debug/*.o $(SRC)/usage/*.o $(SRC)/usage/rl6/*.o $(SRC)/usage/rl7/*.o $(SRC)/throughput/*.o $(SRC)/meta/*.o \
		$(BIN)/poly_count $(BIN)/poly_print $(BIN)/vcomp_count \
		$(BIN)/count_vcomp $(BIN)/vcomp_print $(BIN)/vcomp_parity \
		$(BIN)/imgtable $(BIN)/tile_to_imgtable \
		$(BIN)/rl0_generate $(BIN)/rl0_depict \
		$(BIN)/rl1_generate $(BIN)/rl1_generate_naive $(BIN)/rl1_depict $(BIN)/rl1_depict_naive \
		$(BIN)/rl2_generate $(BIN)/rl3_generate $(BIN)/rl4_generate $(BIN)/rl4_supertile_candidates \
		$(BIN)/rl5_hexagons $(BIN)/rl5_supertile_hexagons \
		$(BIN)/supertile_depict $(BIN)/rl5_refine $(BIN)/rl5_stats \
		$(BIN)/pathological_item6 $(BIN)/rl5_sat $(BIN)/rl6_reduce $(BIN)/rl6_refine $(BIN)/rl6_generate \
		$(BIN)/rl2_refine $(BIN)/rl3_refine $(BIN)/filter_runlevels \
		$(BIN)/hce $(BIN)/rephex $(BIN)/rephex_print $(BIN)/rephex_hacked $(BIN)/rl6_print_conversion $(BIN)/rl7_inflation_print \
		$(BIN)/rl1_hexagons $(BIN)/rl1_search $(BIN)/rl1_refine \
		$(BIN)/search1 $(BIN)/refine1 \
		$(BIN)/rl0_refine \
		$(BIN)/depict_search0_graph \
		$(BIN)/gen_graph \
		$(BIN)/tiling_3x3_demo $(BIN)/tiling_4x4_demo \
		$(BIN)/hat_boundary_dfs_check $(BIN)/check_indexed_variants \
		$(BIN)/check_rl1_tile_order \
		tests/check_rl0 tests/check_rl0_forget_map tests/check_rl0_incremental_graph tests/check_rl1_naive_filtration
	@rm -rf data/run/rl7
	@rm -f img/rl7/rephex/current.svg img/rl7/rephex/current_hat.svg


# -----------------------------------------------------------------------------
# CORE
# -----------------------------------------------------------------------------
# Default build: compile normal tools only.
#

poly_count: $(BIN)/poly_count
$(BIN)/poly_count: $(SRC)/usage/count_poly.o $(CORE) $(POLY_SURFACE) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/usage/count_poly.o $(CORE) $(POLY_SURFACE)

poly_print: $(BIN)/poly_print
$(BIN)/poly_print: $(SRC)/usage/print_poly.o $(CORE) $(POLY_SURFACE) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/usage/print_poly.o $(CORE) $(POLY_SURFACE)

vcomp_count: $(BIN)/vcomp_count
$(BIN)/vcomp_count: $(SRC)/usage/count_vcomp.o $(CORE) $(VCOMP_SURFACE) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/usage/count_vcomp.o $(CORE) $(VCOMP_SURFACE)

count_vcomp: $(BIN)/count_vcomp
$(BIN)/count_vcomp: $(BIN)/vcomp_count | $(BIN)
	@cp $(BIN)/vcomp_count $(BIN)/count_vcomp

vcomp_print: $(BIN)/vcomp_print
$(BIN)/vcomp_print: $(SRC)/usage/print_vcomp.o $(CORE) $(VCOMP_SURFACE) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/usage/print_vcomp.o $(CORE) $(VCOMP_SURFACE)

vcomp_parity: $(BIN)/vcomp_parity
$(BIN)/vcomp_parity: $(SRC)/usage/check_vcomp_parity.o $(CORE) $(VCOMP_SURFACE) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/usage/check_vcomp_parity.o $(CORE) $(VCOMP_SURFACE)


# -----------------------------------------------------------------------------
# Run levels
# -----------------------------------------------------------------------------
# Step through functions for generating efficiency surfaces
#

rl0_generate: $(BIN)/rl0_generate
$(BIN)/rl0_generate: $(SRC)/rl0/generate_rl0.o $(CORE) $(VCOMP_SURFACE) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/rl0/generate_rl0.o $(CORE) $(VCOMP_SURFACE)

rl0_refine: $(BIN)/rl0_refine
$(BIN)/rl0_refine: $(SRC)/rl0/rl0_refine.o $(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/rl0/rl0_refine.o $(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE)

rl1_generate_naive: $(BIN)/rl1_generate_naive
$(BIN)/rl1_generate_naive: $(SRC)/rl1/generate_rl1_naive.o $(CORE) $(VCOMP_SURFACE) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/rl1/generate_rl1_naive.o $(CORE) $(VCOMP_SURFACE)

rl1_generate: $(BIN)/rl1_generate
$(BIN)/rl1_generate: $(SRC)/rl1/generate_rl1.o $(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/rl1/generate_rl1.o $(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE)

rl1_refine: $(BIN)/rl1_refine
$(BIN)/rl1_refine: $(SRC)/rl1/refine1.o $(SRC)/rl1/bcomp1.o $(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/rl1/refine1.o $(SRC)/rl1/bcomp1.o $(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE)

rl2_generate: $(BIN)/rl2_generate
$(BIN)/rl2_generate: $(SRC)/rl2/generate_rl2.o $(SRC)/rl1/bcomp1.o $(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/rl2/generate_rl2.o $(SRC)/rl1/bcomp1.o $(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE)

rl2_refine: rl1_refine
	@$(BIN)/rl1_refine --input data/rl2/completions.dat --output data/rl2/deletions.dat --depth 2

rl3_generate: $(BIN)/rl3_generate
$(BIN)/rl3_generate: $(SRC)/rl3/generate_rl3.o $(SRC)/rl1/bcomp1.o $(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/rl3/generate_rl3.o $(SRC)/rl1/bcomp1.o $(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE)

rl3_refine: rl1_refine
	@$(BIN)/rl1_refine --input data/rl3/completions.dat --output data/rl3/deletions.dat --depth 1

rl4_generate: $(BIN)/rl4_generate
$(BIN)/rl4_generate: $(SRC)/rl4/generate_rl4.o $(SRC)/rl1/bcomp1.o $(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/rl4/generate_rl4.o $(SRC)/rl1/bcomp1.o $(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE)

# refine4.c no longer participates in RL4 throughput. It now serves two RL5/depict roles:
#  1) rl5_supertile_hexagons -> write supertile hexagon/edge data
#  2) supertile_depict    -> render supertile pictures for make figures
supertile_depict: $(BIN)/supertile_depict
$(BIN)/supertile_depict: $(SRC)/rl4/refine4.o $(SRC)/rl1/bcomp1.o $(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/rl4/refine4.o $(SRC)/rl1/bcomp1.o $(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE) -lm

rl4_refine: $(BIN)/rl4_supertile_candidates
$(BIN)/rl4_supertile_candidates: $(SRC)/rl4/supertile_candidates.o | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/rl4/supertile_candidates.o

rl5_refine: $(BIN)/rl5_refine
$(BIN)/rl5_refine: $(SRC)/rl5/refine5.o | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/rl5/refine5.o

pathological_item6: $(BIN)/pathological_item6
$(BIN)/pathological_item6: $(SRC)/usage/pathological_item6.o $(SRC)/rl1/bcomp1.o $(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/usage/pathological_item6.o $(SRC)/rl1/bcomp1.o $(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE)

rl5_sat: $(BIN)/rl5_sat
$(BIN)/rl5_sat: $(SRC)/rl5/sat5.o $(SRC)/rl5/model5.o | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/rl5/sat5.o $(SRC)/rl5/model5.o

RL5_HEXAGONS_DATA ?= data/rl5/hexagons.dat
RL5_SUPERTILE_HEXAGONS_DATA ?= data/rl5/supertile_hexagons.dat
RL5_OVERLAP_HEXAGONS_DATA ?= data/rl5/overlap_supertile_hexagons.dat
RL5_REDUCED_HEXES_DATA ?= data/rl5/reduced_8_hexes.dat
RL5_PATHOLOGY_DATA ?= data/rl5/pathological_item.dat
RL5_SAT_DIR ?= data/rl5/sat
RL5_SUPER6_D6_FIXED_CNF ?= $(RL5_SAT_DIR)/super6_d6_fixed.cnf
RL5_SUPER6_D6_FIXED_MAP ?= $(RL5_SAT_DIR)/super6_d6_fixed.map
RL5_OVERLAP6_D6_FIXED_CNF ?= $(RL5_SAT_DIR)/overlap6_d6_fixed.cnf
RL5_OVERLAP6_D6_FIXED_MAP ?= $(RL5_SAT_DIR)/overlap6_d6_fixed.map
RL5_OVERLAP7_D6_FIXED_CNF ?= $(RL5_SAT_DIR)/overlap7_d6_fixed.cnf
RL5_OVERLAP7_D6_FIXED_MAP ?= $(RL5_SAT_DIR)/overlap7_d6_fixed.map
RL5_OVERLAP12_D6_FIXED_CNF ?= $(RL5_SAT_DIR)/overlap12_d6_fixed.cnf
RL5_OVERLAP12_D6_FIXED_MAP ?= $(RL5_SAT_DIR)/overlap12_d6_fixed.map
RL5_OVERLAP13_D6_FIXED_CNF ?= $(RL5_SAT_DIR)/overlap13_d6_fixed.cnf
RL5_OVERLAP13_D6_FIXED_MAP ?= $(RL5_SAT_DIR)/overlap13_d6_fixed.map
RL5_ERRATA_DATA ?= data/rl5/errata.dat

rl5_hexagons: $(BIN)/rl5_hexagons
$(BIN)/rl5_hexagons: $(SRC)/rl4/hexagons.o $(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/rl4/hexagons.o $(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE)

rl5_supertile_hexagons: $(BIN)/rl5_supertile_hexagons
$(BIN)/rl5_supertile_hexagons: $(SRC)/rl4/refine4.o $(SRC)/rl1/bcomp1.o $(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/rl4/refine4.o $(SRC)/rl1/bcomp1.o $(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE) -lm


rl5_stats: $(BIN)/rl5_stats
$(BIN)/rl5_stats: $(SRC)/rl5/stats5.o $(SRC)/rl5/model5.o $(SRC)/rl5/attach5.o $(SRC)/rl5/vcomp5.o $(SRC)/rl5/bcomp5.o $(SRC)/rl5/boundary5.o $(SRC)/core/term_color.o | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/rl5/stats5.o $(SRC)/rl5/model5.o $(SRC)/rl5/attach5.o $(SRC)/rl5/vcomp5.o $(SRC)/rl5/bcomp5.o $(SRC)/rl5/boundary5.o $(SRC)/core/term_color.o

rl5_generate: rl5_hexagons rl5_supertile_hexagons


rl6_reduce: $(BIN)/rl6_reduce
$(BIN)/rl6_reduce: $(SRC)/rl6/reduce6.o $(SRC)/rl5/model5.o $(SRC)/rl5/attach5.o $(SRC)/rl5/vcomp5.o $(SRC)/rl5/bcomp5.o $(SRC)/rl5/boundary5.o | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/rl6/reduce6.o $(SRC)/rl5/model5.o $(SRC)/rl5/attach5.o $(SRC)/rl5/vcomp5.o $(SRC)/rl5/bcomp5.o $(SRC)/rl5/boundary5.o

rl6_refine: $(BIN)/rl6_refine
$(BIN)/rl6_refine: $(BIN)/rl6_reduce | $(BIN)
	@cp $(BIN)/rl6_reduce $@

rl6_generate: $(BIN)/rl6_generate
$(BIN)/rl6_generate: $(SRC)/rl6/generate6.o | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/rl6/generate6.o

hce: $(BIN)/hce
rl7_collar_reepl: hce
$(BIN)/hce: $(SRC)/usage/rl7/hce.o $(SRC)/rl7/boundary7.o $(SRC)/rl7/comp7.o $(SRC)/rl7/evo7.o $(SRC)/usage/print7.o | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/usage/rl7/hce.o $(SRC)/rl7/boundary7.o $(SRC)/rl7/comp7.o $(SRC)/rl7/evo7.o $(SRC)/usage/print7.o

rephex: $(BIN)/rephex $(BIN)/rephex_print $(BIN)/rephex_hacked
$(BIN)/rephex: $(SRC)/usage/rl7/rephex.o | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/usage/rl7/rephex.o

rephex_print: $(BIN)/rephex_print
$(BIN)/rephex_print: $(SRC)/usage/rl7/rephex_print.o $(SRC)/usage/rl7/rephex_hat.o $(SRC)/rl7/inflation7.o | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/usage/rl7/rephex_print.o $(SRC)/usage/rl7/rephex_hat.o $(SRC)/rl7/inflation7.o -lm

rephex_hacked: $(BIN)/rephex_hacked
$(BIN)/rephex_hacked: $(SRC)/usage/rl7/rephex_hacked.o | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/usage/rl7/rephex_hacked.o -lm

rl7_inflation_figures: rephex_print
	@$(SRC)/usage/rl7/render_inflation_gallery.sh img/rl7/inflation

RL6_UNIFIED_DATA ?= data/rl6/unified_model.dat
RL6_REFINED_DATA ?= data/rl6/refined_model.dat
RL6_REFINE_CANCEL_DEPTH ?= 1

rl6_unified_data: rl6_reduce rl6_generate
	@mkdir -p data/rl6
	@$(BIN)/rl6_generate $(RL6_UNIFIED_DATA)

rl6_refine_data: rl6_refine rl6_unified_data
	@mkdir -p data/rl6
	@$(BIN)/rl6_refine --cancel-depth $(RL6_REFINE_CANCEL_DEPTH) --output $(RL6_REFINED_DATA)


# -----------------------------------------------------------------------------
# BOOT
# -----------------------------------------------------------------------------
# Boot tower: purge generated runlevel data, then rebuild a deterministic
# prefix of the boot chain.
#
#  make boot      # full boot, reaching RL6
#  make boot RL=0 # full boot, reaching RL6
#  make boot RL=1 # stop after reaching RL1
#  make boot RL=2 # stop after reaching RL2
#  make boot RL=3 # stop after reaching RL3
#  make boot RL=4 # stop after reaching RL4
#  make boot RL=5 # stop after reaching RL5
#  make boot RL=6 # stop after reaching RL6
#

RL ?= 0
RL3_REFINE_DEPTH ?= 1

ifeq ($(RL),0)
BOOT_RUNLEVELS := boot_rl0 boot_rl1 boot_rl2 boot_rl3 boot_rl4 boot_rl5 boot_rl6
BOOT_REACHED_RL := 7
else ifeq ($(RL),1)
BOOT_RUNLEVELS := boot_rl0
BOOT_REACHED_RL := 1
else ifeq ($(RL),2)
BOOT_RUNLEVELS := boot_rl0 boot_rl1
BOOT_REACHED_RL := 2
else ifeq ($(RL),3)
BOOT_RUNLEVELS := boot_rl0 boot_rl1 boot_rl2
BOOT_REACHED_RL := 3
else ifeq ($(RL),4)
BOOT_RUNLEVELS := boot_rl0 boot_rl1 boot_rl2 boot_rl3
BOOT_REACHED_RL := 4
else ifeq ($(RL),5)
BOOT_RUNLEVELS := boot_rl0 boot_rl1 boot_rl2 boot_rl3 boot_rl4
BOOT_REACHED_RL := 5
else ifeq ($(RL),6)
BOOT_RUNLEVELS := boot_rl0 boot_rl1 boot_rl2 boot_rl3 boot_rl4 boot_rl5
BOOT_REACHED_RL := 6
else
$(error Unknown RL=$(RL); use RL=0 for full boot or RL=1..6 to reach that runlevel)
endif

boot: clean_boot_data $(BOOT_RUNLEVELS) boot_splash

boot_splash:
	$(call announce,boot successful$(comma) you've reached RL$(BOOT_REACHED_RL))
	@cat $(SPLASH)

splash:
	@cat $(SPLASH)

clean_boot_data:
	$(call announce,purge data/rl*/*)
	@if [ -d data ]; then find data -depth -mindepth 2 -path 'data/rl*/*' -delete; fi

boot_rl0: rl0_generate rl0_refine
	$(call announce,RL0 generate)
	@$(BIN)/rl0_generate preferences/focus.tile
	$(call announce,RL0 refine)
	@$(BIN)/rl0_refine

boot_rl1: rl1_generate rl1_refine
	$(call announce,RL1 generate)
	@mkdir -p data/rl1
	@$(BIN)/rl1_generate preferences/focus.tile data/rl1/completions.dat 20 --live-only
	$(call announce,RL1 refine)
	@$(BIN)/rl1_refine

boot_rl2: rl2_generate rl1_refine
	$(call announce,RL2 generate)
	@$(BIN)/rl2_generate
	$(call announce,RL2 refine)
	@$(BIN)/rl1_refine --input data/rl2/completions.dat --output data/rl2/deletions.dat --depth 2

boot_rl3: rl3_generate rl1_refine
	$(call announce,RL3 generate)
	@$(BIN)/rl3_generate
	$(call announce,RL3 refine)
	@$(BIN)/rl1_refine --input data/rl3/completions.dat --output data/rl3/deletions.dat --depth $(RL3_REFINE_DEPTH)

boot_rl4: rl4_generate rl4_refine
	$(call announce,RL4 filter)
	@$(BIN)/rl4_generate
	$(call announce,RL4 refine)
	@$(BIN)/rl4_supertile_candidates

boot_rl5: rl5_generate pathological_item6
	$(call announce,RL5 hexagons)
	@mkdir -p data/rl5
	@$(BIN)/rl5_hexagons > $(RL5_HEXAGONS_DATA)
	@$(BIN)/rl5_supertile_hexagons --data > $(RL5_SUPERTILE_HEXAGONS_DATA)
	@$(BIN)/rl5_supertile_hexagons --data --supertile preferences/overlap.supertile > $(RL5_OVERLAP_HEXAGONS_DATA)
	@echo $(RL5_HEXAGONS_DATA)
	@echo $(RL5_SUPERTILE_HEXAGONS_DATA)
	@echo $(RL5_OVERLAP_HEXAGONS_DATA)
	@$(BIN)/pathological_item6 --data $(RL5_PATHOLOGY_DATA) >/dev/null 2>/dev/null 2>/dev/null
	@$(MAKE) --no-print-directory rl5_sat_data
	$(call announce,RL5 pathology)
	@echo $(RL5_PATHOLOGY_DATA)
	@$(MAKE) --no-print-directory rl5_super6_errata_placeholder
	@echo $(RL5_ERRATA_DATA)


boot_rl6: rl6_refine rl6_unified_data
	$(call announce,RL6 refine)
	@mkdir -p data/rl6
	@$(BIN)/rl6_refine --cancel-depth $(RL6_REFINE_CANCEL_DEPTH) --output $(RL6_REFINED_DATA)
	@echo $(RL6_UNIFIED_DATA)
	@echo $(RL6_REFINED_DATA)
	@$(MAKE) --no-print-directory rl6_print_conversion

RL6_PRINT_CONVERSION_DATA := data/rl6/hat/print_conversion.dat

rl6_print_conversion: $(BIN)/rl6_print_conversion rl6_reduce
	@$(BIN)/rl6_print_conversion $(RL6_PRINT_CONVERSION_DATA)

$(BIN)/rl6_print_conversion: $(SRC)/usage/rl6/generate_print_conversion.o | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/usage/rl6/generate_print_conversion.o

rl5_super6_errata_placeholder:
	@mkdir -p $(dir $(RL5_ERRATA_DATA))
	@{ \
		printf "dead: super 6 overlap 6 7 12 13\n\n"; \
		printf "Generated SAT artifacts are intentionally not tracked.\n\n"; \
		printf "To regenerate:\n"; \
		printf "  make rl5_super6_sat_data\n"; \
		printf "  make rl5_overlap671213_sat_data\n\n"; \
		printf "To verify:\n"; \
		printf "  make rl5_super6_sat_check\n"; \
		printf "  make rl5_overlap671213_sat_check\n"; \
	} > $(RL5_ERRATA_DATA)

rl5_sat_data: rl5_super6_sat_data rl5_overlap671213_sat_data

rl5_super6_sat_data: rl5_sat
	@mkdir -p $(RL5_SAT_DIR)
	@$(BIN)/rl5_sat --model super --record 6 --boundary-depth 6 --fixed-center-orientation --cnf $(RL5_SUPER6_D6_FIXED_CNF) --map $(RL5_SUPER6_D6_FIXED_MAP) >/dev/null 2>/dev/null

rl5_overlap671213_sat_data: rl5_sat
	@mkdir -p $(RL5_SAT_DIR)
	@$(BIN)/rl5_sat --model overlap --record 6 --boundary-depth 6 --fixed-center-orientation --cnf $(RL5_OVERLAP6_D6_FIXED_CNF) --map $(RL5_OVERLAP6_D6_FIXED_MAP) >/dev/null 2>/dev/null
	@$(BIN)/rl5_sat --model overlap --record 7 --boundary-depth 6 --fixed-center-orientation --cnf $(RL5_OVERLAP7_D6_FIXED_CNF) --map $(RL5_OVERLAP7_D6_FIXED_MAP) >/dev/null 2>/dev/null
	@$(BIN)/rl5_sat --model overlap --record 12 --boundary-depth 6 --fixed-center-orientation --cnf $(RL5_OVERLAP12_D6_FIXED_CNF) --map $(RL5_OVERLAP12_D6_FIXED_MAP) >/dev/null 2>/dev/null
	@$(BIN)/rl5_sat --model overlap --record 13 --boundary-depth 6 --fixed-center-orientation --cnf $(RL5_OVERLAP13_D6_FIXED_CNF) --map $(RL5_OVERLAP13_D6_FIXED_MAP) >/dev/null 2>/dev/null

rl5_super6_sat_check: rl5_super6_sat_data
	@tools/rl5_count_pysat.py --boundary-depth 6 --skip-generate --cnf $(RL5_SUPER6_D6_FIXED_CNF) --map $(RL5_SUPER6_D6_FIXED_MAP) --exists-only | \
		awk '/^SAT$$/{bad=1} END{if(bad){print "super 6: SAT"; exit 1} print "super 6: UNSAT"}'
	@echo $(RL5_ERRATA_DATA)

rl5_overlap671213_sat_check: rl5_overlap671213_sat_data
	@tools/rl5_count_pysat.py --boundary-depth 6 --skip-generate --cnf $(RL5_OVERLAP6_D6_FIXED_CNF) --map $(RL5_OVERLAP6_D6_FIXED_MAP) --exists-only | awk '/^SAT$$/{bad=1} END{if(bad){print "overlap 6: SAT"; exit 1} print "overlap 6: UNSAT"}'
	@tools/rl5_count_pysat.py --boundary-depth 6 --skip-generate --cnf $(RL5_OVERLAP7_D6_FIXED_CNF) --map $(RL5_OVERLAP7_D6_FIXED_MAP) --exists-only | awk '/^SAT$$/{bad=1} END{if(bad){print "overlap 7: SAT"; exit 1} print "overlap 7: UNSAT"}'
	@tools/rl5_count_pysat.py --boundary-depth 6 --skip-generate --cnf $(RL5_OVERLAP12_D6_FIXED_CNF) --map $(RL5_OVERLAP12_D6_FIXED_MAP) --exists-only | awk '/^SAT$$/{bad=1} END{if(bad){print "overlap 12: SAT"; exit 1} print "overlap 12: UNSAT"}'
	@tools/rl5_count_pysat.py --boundary-depth 6 --skip-generate --cnf $(RL5_OVERLAP13_D6_FIXED_CNF) --map $(RL5_OVERLAP13_D6_FIXED_MAP) --exists-only | awk '/^SAT$$/{bad=1} END{if(bad){print "overlap 13: SAT"; exit 1} print "overlap 13: UNSAT"}'
	@echo $(RL5_ERRATA_DATA)

rl5_sat_solver_notice:
	@printf "SAT verification uses: make rl5_super6_sat_check or make rl5_overlap671213_sat_check\n"

rl0_data: rl0_generate
	@$(BIN)/rl0_generate preferences/focus.tile

rl1_data: rl1_generate
	@mkdir -p data/rl1
	@$(BIN)/rl1_generate preferences/focus.tile data/rl1/completions.dat 20 --live-only

# needed for testing, not strict boot
rl1_data_naive: rl1_generate_naive
	@mkdir -p data/rl1
	@$(BIN)/rl1_generate_naive preferences/focus.tile data/rl1/naive_completions.dat 20 --live-only

meta_graph_data: meta_graph
	@mkdir -p data/meta
	@$(BIN)/gen_graph src > data/meta/dependencies.txt

# -----------------------------------------------------------------------------
# TEST
# -----------------------------------------------------------------------------
# Validation of core functionality and data on subsequent runlevels
#

checks: check_rl0 check_rl0_forget_map check_rl0_incremental_graph \
	check_rl1_naive_filtration check_rl1_tile_order

test: tools checks
	./tests/smoke.sh

check_rl0: tests/check_rl0
tests/check_rl0: tests/check_rl0.c
	$(CC) -O2 -Wall -Wextra -std=c11 tests/check_rl0.c -o $@

check_rl0_remembrance_counts: rl0_data
	python3 tests/check_rl0_remembrance_counts.py data/rl0/remembrance.dat data/rl0/remembrance_counts.txt

check_rl0_forget_map: tests/check_rl0_forget_map
tests/check_rl0_forget_map: tests/check_rl0_forget_map.c $(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE)
	$(CC) $(CFLAGS) -I$(SRC) -o $@ tests/check_rl0_forget_map.c \
		$(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE)

check_rl0_incremental_graph: tests/check_rl0_incremental_graph
tests/check_rl0_incremental_graph: tests/check_rl0_incremental_graph.c $(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE)
	$(CC) $(CFLAGS) -I$(SRC) -o $@ tests/check_rl0_incremental_graph.c \
		$(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE)

check_rl1_naive_filtration: tests/check_rl1_naive_filtration
tests/check_rl1_naive_filtration: tests/check_rl1_naive_filtration.c $(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE)
	$(CC) $(CFLAGS) -I$(SRC) -o $@ tests/check_rl1_naive_filtration.c \
		$(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE)

check_rl1_tile_order: $(BIN)/check_rl1_tile_order
$(BIN)/check_rl1_tile_order: $(SRC)/usage/check_rl1_tile_order.o $(SRC)/rl1/bcomp1.o $(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/usage/check_rl1_tile_order.o $(SRC)/rl1/bcomp1.o $(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE)

check_indexed_variants: $(BIN)/check_indexed_variants
$(BIN)/check_indexed_variants: $(SRC)/usage/check_indexed_variants.o $(SRC)/core/tile.o \
	$(SRC)/core/cycle.o $(SRC)/core/tetrille.o $(SRC)/core/lattice.o | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/usage/check_indexed_variants.o \
		$(SRC)/core/tile.o $(SRC)/core/cycle.o \
		$(SRC)/core/tetrille.o $(SRC)/core/lattice.o

hat_boundary_dfs_check: $(BIN)/hat_boundary_dfs_check
$(BIN)/hat_boundary_dfs_check: $(SRC)/usage/hat_boundary_dfs_check.o $(CORE) $(SRC)/core/dfs.o | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/usage/hat_boundary_dfs_check.o \
		$(CORE) $(SRC)/core/dfs.o

# -----------------------------------------------------------------------------
# MyHatTileStuff - personal experiments
# -----------------------------------------------------------------------------

MY_HAT = MyHatTileStuff

$(MY_HAT)/%.o: $(MY_HAT)/%.c
	$(CC) $(CFLAGS) -I$(SRC) -c $< -o $@

hat_three_no_rotation: $(BIN)/hat_three_no_rotation
$(BIN)/hat_three_no_rotation: $(MY_HAT)/hat_three_no_rotation.o $(CORE) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(MY_HAT)/hat_three_no_rotation.o $(CORE)

hat_four_no_rotation: $(BIN)/hat_four_no_rotation
$(BIN)/hat_four_no_rotation: $(MY_HAT)/hat_four_no_rotation.o $(CORE) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(MY_HAT)/hat_four_no_rotation.o $(CORE)

surrounding: $(BIN)/surrounding
$(BIN)/surrounding: $(MY_HAT)/surrounding.o $(CORE) | $(BIN)
	$(CC) $(CFLAGS) -lm -o $@ $(MY_HAT)/surrounding.o $(CORE)

surrounding2: $(BIN)/surrounding2
$(BIN)/surrounding2: $(MY_HAT)/surrounding2.o $(CORE) | $(BIN)
	$(CC) $(CFLAGS) -lm -o $@ $(MY_HAT)/surrounding2.o $(CORE)

centers: $(BIN)/centers
$(BIN)/centers: $(MY_HAT)/centers.o $(CORE) | $(BIN)
	$(CC) $(CFLAGS) -lm -o $@ $(MY_HAT)/centers.o $(CORE)


figures:
	$(call announce,generate figures)
	@$(MAKE) --no-print-directory figures_files

figures_files: rl0_svg rl0_refine_svg rl1_survivors_svg rl2_survivors_svg rl3_survivors_svg supertile_hex_models_svg supertile_svg overlap_supertile_svg rl5_pathology_svg

# -----------------------------------------------------------------------------
# DEPICT
# -----------------------------------------------------------------------------
# Figures for a paper?
#

rl0_svg: rl0_data rl0_depict imgtable
	@mkdir -p img
	@$(BIN)/rl0_depict --data data/rl0/completions.dat --grouped | \
		$(BIN)/imgtable > img/rl0_all.svg
	@echo img/rl0_all.svg

RL0_SEARCH_GRAPH ?= data/rl0/search_graph.dat
RL0_REFINE_SVG ?= img/rl0_refine.svg

rl0_refine_svg: depict_search0_graph
	@test -s $(RL0_SEARCH_GRAPH) || { \
		echo "missing $(RL0_SEARCH_GRAPH); run: make boot RL=1" >&2; \
		exit 1; \
	}
	@mkdir -p $(dir $(RL0_REFINE_SVG))
	@$(BIN)/depict_search0_graph --colors < $(RL0_SEARCH_GRAPH) > $(RL0_REFINE_SVG)
	@echo $(RL0_REFINE_SVG)

rl0_refine_svg_print: depict_search0_graph
	@test -s $(RL0_SEARCH_GRAPH) || { \
		echo "missing $(RL0_SEARCH_GRAPH); run: make boot RL=1" >&2; \
		exit 1; \
	}
	@mkdir -p $(dir $(RL0_REFINE_SVG))
	@$(BIN)/depict_search0_graph --colors < $(RL0_SEARCH_GRAPH) | tee $(RL0_REFINE_SVG)

rl1_svg_naive: rl1_data_naive rl1_depict imgtable
	mkdir -p img
	$(BIN)/rl1_depict preferences/focus.tile data/rl1/naive_completions.dat --grouped | \
		$(BIN)/imgtable > img/rl1_naive_completions.svg

rl1_survivors_svg: rl1_depict imgtable
	@test -s data/rl4/rl1_filtered.dat || { \
		echo "missing data/rl4/rl1_filtered.dat; run: make boot" >&2; \
		exit 1; \
	}
	@mkdir -p img/
	@$(BIN)/rl1_depict preferences/focus.tile data/rl4/rl1_filtered.dat --grouped | \
		$(BIN)/imgtable > img/rl1_survivors.svg
	@echo img/rl1_survivors.svg

rl2_survivors_svg: rl1_depict imgtable
	@test -s data/rl4/rl2_filtered.dat || { \
		echo "missing data/rl4/rl2_filtered.dat; run: make boot" >&2; \
		exit 1; \
	}
	@mkdir -p img/
	@$(BIN)/rl1_depict preferences/focus.tile data/rl4/rl2_filtered.dat --grouped | \
		$(BIN)/imgtable > img/rl2_survivors.svg
	@echo img/rl2_survivors.svg

rl3_survivors_svg: rl1_depict imgtable
	@test -s data/rl4/rl3_filtered.dat || { \
		echo "missing data/rl4/rl3_filtered.dat; run: make boot" >&2; \
		exit 1; \
	}
	@mkdir -p img/
	@$(BIN)/rl1_depict preferences/focus.tile data/rl4/rl3_filtered.dat --grouped | \
		$(BIN)/imgtable > img/rl3_survivors.svg
	@echo img/rl3_survivors.svg

SUPERTILE_DEPICT_DATA ?= data/rl4/rl3_filtered.dat
SUPERTILE_HEX_MODELS_SUPERTILE ?= preferences/focus.supertile
SUPERTILE_HEX_MODELS_SVG ?= img/rl4_refine.svg
SUPERTILE_SVG ?= img/supertile.svg
OVERLAP_SUPERTILE_SVG ?= img/overlap_supertile.svg
RL5_PATHOLOGY_SVG ?= img/rl5_pathology.svg

# Compatibility alias. The clearer name is supertile_hex_models_svg.
: supertile_hex_models_svg

supertile_hex_models_svg: supertile_depict imgtable
	@test -s $(SUPERTILE_DEPICT_DATA) || { \
		echo "missing $(SUPERTILE_DEPICT_DATA); run: make boot" >&2; \
		exit 1; \
	}
	@test -s $(SUPERTILE_HEX_MODELS_SUPERTILE) || { \
		echo "missing $(SUPERTILE_HEX_MODELS_SUPERTILE)" >&2; \
		exit 1; \
	}
	@mkdir -p $(dir $(SUPERTILE_HEX_MODELS_SVG))
	@$(BIN)/supertile_depict preferences/focus.tile $(SUPERTILE_DEPICT_DATA) $(SUPERTILE_HEX_MODELS_SUPERTILE) | \
		$(BIN)/imgtable > $(SUPERTILE_HEX_MODELS_SVG)
	@echo $(SUPERTILE_HEX_MODELS_SVG)

supertile_svg: rl1_depict imgtable
	@test -s preferences/focus.supertile || { \
		echo "missing preferences/focus.supertile" >&2; \
		exit 1; \
	}
	@mkdir -p $(dir $(SUPERTILE_SVG))
	@$(BIN)/rl1_depict preferences/focus.tile preferences/focus.supertile --grouped | \
		$(BIN)/imgtable > $(SUPERTILE_SVG)
	@echo $(SUPERTILE_SVG)

overlap_supertile_svg: rl1_depict imgtable
	@test -s preferences/overlap.supertile || { \
		echo "missing preferences/overlap.supertile" >&2; \
		exit 1; \
	}
	@mkdir -p $(dir $(OVERLAP_SUPERTILE_SVG))
	@$(BIN)/rl1_depict preferences/focus.tile preferences/overlap.supertile --grouped | \
		$(BIN)/imgtable > $(OVERLAP_SUPERTILE_SVG)
	@echo $(OVERLAP_SUPERTILE_SVG)


rl5_pathology_svg: rl1_depict imgtable pathological_item6
	@test -s $(RL5_PATHOLOGY_DATA) || { \
		echo "missing $(RL5_PATHOLOGY_DATA); run: make boot RL=6" >&2; \
		exit 1; \
	}
	@mkdir -p $(dir $(RL5_PATHOLOGY_SVG))
	@tmp=$$(mktemp); \
	$(BIN)/rl1_depict preferences/focus.tile $(RL5_PATHOLOGY_DATA) --grouped > $$tmp; \
	awk 'BEGIN { agg=""; have=0 } /^Aggregate$$/ { print; if ((getline line) > 0) { agg=line; have=1; print line; } next } /^Hidden$$/ { if (have) { print "Highlights"; print agg; have=0; } print; next } { print } END { if (have) { print "Highlights"; print agg; } }' $$tmp | $(BIN)/imgtable > $(RL5_PATHOLOGY_SVG); \
	rm -f $$tmp
	@echo $(RL5_PATHOLOGY_SVG)

imgtable: $(BIN)/imgtable
$(BIN)/imgtable: $(SRC)/usage/imgtable.o | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/usage/imgtable.o -lm

tile_to_imgtable: $(BIN)/tile_to_imgtable
$(BIN)/tile_to_imgtable: $(SRC)/usage/tile_to_imgtable.o $(SRC)/core/tile.o \
	$(SRC)/core/cycle.o $(SRC)/core/tetrille.o $(SRC)/core/lattice.o
	$(CC) $(CFLAGS) -o $@ $(SRC)/usage/tile_to_imgtable.o \
		$(SRC)/core/tile.o $(SRC)/core/cycle.o \
		$(SRC)/core/tetrille.o $(SRC)/core/lattice.o

rl0_depict: $(BIN)/rl0_depict
$(BIN)/rl0_depict: $(SRC)/usage/depict_rl0.o $(CORE) $(SRC)/throughput/vcomp.o | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/usage/depict_rl0.o $(CORE) $(SRC)/throughput/vcomp.o

rl1_depict: $(BIN)/rl1_depict
$(BIN)/rl1_depict: $(SRC)/usage/depict_rl1.o $(RL0_SURFACE) $(CORE) $(SRC)/throughput/vcomp.o | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/usage/depict_rl1.o $(RL0_SURFACE) $(CORE) $(SRC)/throughput/vcomp.o

depict_search0_graph: $(BIN)/depict_search0_graph
$(BIN)/depict_search0_graph: $(SRC)/usage/depict_search0_graph.o | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/usage/depict_search0_graph.o -lm


# -----------------------------------------------------------------------------
# EXTRA
# -----------------------------------------------------------------------------
# Historical, audit, debug, and non-boot tools. These are kept buildable, but
# should not control the normal develop/debug loop.



rl1_hexagons: $(BIN)/rl1_hexagons
$(BIN)/rl1_hexagons: $(SRC)/rl4/hexagons.o $(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/rl4/hexagons.o $(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE)

rl1_search: $(BIN)/rl1_search
$(BIN)/rl1_search: $(SRC)/rl1/search1.o $(SRC)/rl1/bcomp1.o $(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/rl1/search1.o $(SRC)/rl1/bcomp1.o $(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE)


# Backward-compatible build aliases; binaries use contextual rl1_* names.
search1: rl1_search
refine1: rl1_refine

search1_debug: $(BIN)/search1_debug
$(BIN)/search1_debug: $(SRC)/rl1/debug/search1_debug.o $(SRC)/rl1/bcomp1.o $(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/rl1/debug/search1_debug.o $(SRC)/rl1/bcomp1.o $(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE)

rl0_stats: $(BIN)/rl0_stats
$(BIN)/rl0_stats: $(SRC)/usage/rl0_stats.o $(CORE) $(SRC)/throughput/vcomp.o | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/usage/rl0_stats.o $(CORE) $(SRC)/throughput/vcomp.o

meta_graph: $(BIN)/gen_graph
$(BIN)/gen_graph: $(SRC)/meta/gen_graph.o | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/meta/gen_graph.o

tiling_3x3_demo: $(BIN)/tiling_3x3_demo
$(BIN)/tiling_3x3_demo: $(SRC)/usage/tiling_3x3_demo.o $(SRC)/core/dfs.o | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/usage/tiling_3x3_demo.o \
		$(SRC)/core/dfs.o

tiling_4x4_demo: $(BIN)/tiling_4x4_demo
$(BIN)/tiling_4x4_demo: $(SRC)/usage/tiling_4x4_demo.o $(SRC)/core/dfs.o | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/usage/tiling_4x4_demo.o \
		$(SRC)/core/dfs.o

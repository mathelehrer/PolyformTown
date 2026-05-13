CC = gcc
CFLAGS = -O3 -Wall -Wextra -std=c11
SRC = src
BIN = bin

CORE = $(SRC)/core/cycle.o $(SRC)/core/tile.o $(SRC)/core/hash.o \
       $(SRC)/core/vec.o $(SRC)/core/attach.o \
       $(SRC)/core/tetrille.o $(SRC)/core/lattice.o \
       $(SRC)/core/boundary.o $(SRC)/core/numerics.o
POLY_SURFACE = $(SRC)/throughput/vcomp.o $(SRC)/throughput/poly_pipeline.o
VCOMP_SURFACE = $(SRC)/throughput/vcomp.o $(SRC)/throughput/vcomp_pipeline.o
RL0_SURFACE = $(SRC)/rl0/forget_map.o $(SRC)/rl0/boundary0.o $(SRC)/rl0/vcomp0.o $(SRC)/rl0/attach0.o

.PHONY: all tools data boot clean_boot_data boot_rl0 boot_rl1 boot_rl2 boot_rl3 boot_rl4 \
	checks test clean \
	poly_count poly_print vcomp_count count_vcomp vcomp_print vcomp_parity \
	rl0_generate rl0_refine rl1_generate rl1_refine rl2_generate rl2_refine \
	rl3_generate rl3_refine rl4_generate \
	imgtable tile_to_imgtable rl0_depict rl1_depict depict_search0_graph \
	rl0_svg rl0_refine_svg rl1_svg_naive \
	meta_graph tiling_3x3_demo tiling_4x4_demo hat_boundary_dfs_check \
	check_indexed_variants check_rl1_tile_order search1 refine1

# -----------------------------------------------------------------------------
# All
# -----------------------------------------------------------------------------
# A maximal default build surface
#

all: tools

tools: poly_count poly_print vcomp_count vcomp_print vcomp_parity \
	rl0_generate rl0_refine rl1_generate rl1_refine rl2_generate rl2_refine \
	rl3_generate rl3_refine rl4_generate \
	imgtable tile_to_imgtable rl0_depict rl1_depict depict_search0_graph \
	rl1_generate_naive rl1_supertiles rl1_hexagons rl1_search rl1_refine_all \
	meta_graph tiling_3x3_demo tiling_4x4_demo \
	hat_boundary_dfs_check check_indexed_variants

data: rl0_data rl1_data rl1_data_naive meta_graph_data

$(SRC)/%.o: $(SRC)/%.c
	$(CC) $(CFLAGS) -I$(SRC) -c $< -o $@

$(BIN):
	mkdir -p $(BIN)

clean:
	rm -f $(SRC)/*.o $(SRC)/core/*.o $(SRC)/rl0/*.o \
		$(SRC)/rl1/*.o $(SRC)/rl2/*.o $(SRC)/rl3/*.o $(SRC)/rl4/*.o $(SRC)/rl1/debug/*.o $(SRC)/usage/*.o $(SRC)/throughput/*.o $(SRC)/meta/*.o \
		$(BIN)/poly_count $(BIN)/poly_print $(BIN)/vcomp_count \
		$(BIN)/count_vcomp $(BIN)/vcomp_print $(BIN)/vcomp_parity \
		$(BIN)/imgtable $(BIN)/tile_to_imgtable \
		$(BIN)/rl0_generate $(BIN)/rl0_depict \
		$(BIN)/rl1_generate $(BIN)/rl1_generate_naive $(BIN)/rl1_depict $(BIN)/rl1_depict_naive \
		$(BIN)/rl2_generate $(BIN)/rl3_generate $(BIN)/rl4_generate \
		$(BIN)/rl2_refine $(BIN)/rl3_refine $(BIN)/filter_runlevels \
		$(BIN)/rl1_supertiles \
		$(BIN)/rl1_hexagons $(BIN)/rl1_search $(BIN)/rl1_refine $(BIN)/rl1_refine_all \
		$(BIN)/search1 $(BIN)/refine1 \
		$(BIN)/rl0_refine \
		$(BIN)/depict_search0_graph \
		$(BIN)/gen_graph \
		$(BIN)/tiling_3x3_demo $(BIN)/tiling_4x4_demo \
		$(BIN)/hat_boundary_dfs_check $(BIN)/check_indexed_variants \
		$(BIN)/check_rl1_tile_order \
		tests/check_rl0 tests/check_rl0_forget_map tests/check_rl0_incremental_graph tests/check_rl1_naive_filtration


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
	cp $(BIN)/vcomp_count $(BIN)/count_vcomp

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
	$(BIN)/rl1_refine --input data/rl2/completions.dat --output data/rl2/deletions.dat --depth 1

rl3_generate: $(BIN)/rl3_generate
$(BIN)/rl3_generate: $(SRC)/rl3/generate_rl3.o $(SRC)/rl1/bcomp1.o $(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/rl3/generate_rl3.o $(SRC)/rl1/bcomp1.o $(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE)

rl3_refine: rl1_refine
	$(BIN)/rl1_refine --input data/rl3/completions.dat --output data/rl3/deletions.dat --depth 1

rl4_generate: $(BIN)/rl4_generate
$(BIN)/rl4_generate: $(SRC)/rl4/generate_rl4.o $(SRC)/rl1/bcomp1.o $(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/rl4/generate_rl4.o $(SRC)/rl1/bcomp1.o $(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE)


# -----------------------------------------------------------------------------
# BOOT
# -----------------------------------------------------------------------------
# Boot tower: purge generated runlevel data, then rebuild a deterministic
# prefix of the boot chain.
#
#   make boot       # full boot, through RL4
#   make boot RL=0  # full boot, through RL4
#   make boot RL=1  # stop after RL0; do not start RL1 generation
#   make boot RL=2  # stop after RL1; do not start RL2 generation
#   make boot RL=3  # stop after RL2; do not start RL3 generation
#   make boot RL=4  # stop after RL3; do not start RL4 generation
#

RL ?= 0

ifeq ($(RL),0)
BOOT_RUNLEVELS := boot_rl0 boot_rl1 boot_rl2 boot_rl3 boot_rl4
else ifeq ($(RL),1)
BOOT_RUNLEVELS := boot_rl0
else ifeq ($(RL),2)
BOOT_RUNLEVELS := boot_rl0 boot_rl1
else ifeq ($(RL),3)
BOOT_RUNLEVELS := boot_rl0 boot_rl1 boot_rl2
else ifeq ($(RL),4)
BOOT_RUNLEVELS := boot_rl0 boot_rl1 boot_rl2 boot_rl3
else
$(error Unknown RL=$(RL); use RL=0 for full boot or RL=1..4 to stop before that runlevel)
endif

boot: clean_boot_data $(BOOT_RUNLEVELS)

clean_boot_data:
	@echo "==> purge data/rl*/*"
	@if [ -d data ]; then find data -depth -mindepth 2 -path 'data/rl*/*' -delete; fi

boot_rl0: rl0_generate rl0_refine
	@echo "==> RL0 generate"
	$(BIN)/rl0_generate preferences/focus.tile
	@echo "==> RL0 refine"
	$(BIN)/rl0_refine

boot_rl1: rl1_generate rl1_refine
	@echo "==> RL1 generate completions"
	@mkdir -p data/rl1
	$(BIN)/rl1_generate preferences/focus.tile data/rl1/completions.dat 20 --live-only
	@echo "==> RL1 refine deletions"
	$(BIN)/rl1_refine

boot_rl2: rl2_generate rl1_refine
	@echo "==> RL2 generate six children"
	$(BIN)/rl2_generate
	@echo "==> RL2 refine deletions"
	$(BIN)/rl1_refine --input data/rl2/completions.dat --output data/rl2/deletions.dat --depth 1

boot_rl3: rl3_generate rl1_refine
	@echo "==> RL3 generate next ring"
	$(BIN)/rl3_generate
	@echo "==> RL3 refine deletions"
	$(BIN)/rl1_refine --input data/rl3/completions.dat --output data/rl3/deletions.dat --depth 1

boot_rl4: rl4_generate
	@echo "==> RL4 materialize filtered runlevel data"
	$(BIN)/rl4_generate

rl0_data: rl0_generate
	$(BIN)/rl0_generate preferences/focus.tile

rl1_data: rl1_generate
	mkdir -p data/rl1
	$(BIN)/rl1_generate preferences/focus.tile data/rl1/completions.dat 20 --live-only

# needed for testing, not strict boot
rl1_data_naive: rl1_generate_naive
	mkdir -p data/rl1
	$(BIN)/rl1_generate_naive preferences/focus.tile data/rl1/naive_completions.dat 20 --live-only

meta_graph_data: meta_graph
	mkdir -p data/meta
	$(BIN)/gen_graph src > data/meta/dependencies.txt

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
# DEPICT
# -----------------------------------------------------------------------------
# Figures for a paper?
#

rl0_svg: rl0_data rl0_depict imgtable
	$(BIN)/rl0_depict --data data/rl0/completions.dat --grouped | \
		$(BIN)/imgtable > img/rl0_all.svg

rl0_refine_svg: rl0_refine depict_search0_graph
	mkdir -p img
	$(BIN)/rl0_refine --print | $(BIN)/depict_search0_graph --colors > img/rl0_refine.svg

rl1_svg_naive: rl1_data_naive rl1_depict imgtable
	mkdir -p img
	$(BIN)/rl1_depict preferences/focus.tile data/rl1/naive_completions.dat --grouped | \
		$(BIN)/imgtable > img/rl1_naive_completions.svg

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
# Historical, audit, debug, and non-boot tools.  These are kept buildable, but
# should not control the normal develop/debug loop.


rl1_supertiles: $(BIN)/rl1_supertiles
$(BIN)/rl1_supertiles: $(SRC)/rl1/supertiles.o $(SRC)/rl1/bcomp1.o $(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/rl1/supertiles.o $(SRC)/rl1/bcomp1.o $(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE)

rl1_hexagons: $(BIN)/rl1_hexagons
$(BIN)/rl1_hexagons: $(SRC)/rl1/hexagons.o $(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/rl1/hexagons.o $(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE)

rl1_search: $(BIN)/rl1_search
$(BIN)/rl1_search: $(SRC)/rl1/search1.o $(SRC)/rl1/bcomp1.o $(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/rl1/search1.o $(SRC)/rl1/bcomp1.o $(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE)

rl1_refine_all: $(BIN)/rl1_refine_all
$(BIN)/rl1_refine_all: $(SRC)/rl1/refine_all.o $(SRC)/rl1/bcomp1.o $(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/rl1/refine_all.o $(SRC)/rl1/bcomp1.o $(RL0_SURFACE) $(CORE) $(VCOMP_SURFACE)

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

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

all: poly_count poly_print vcomp_count vcomp_print vcomp_parity imgtable \
	tile_to_imgtable rl0_generate rl0_depict rl0_data tiling_3x3_demo \
	tiling_4x4_demo hat_boundary_dfs_check check_rl0

poly_count: $(BIN)/poly_count
$(BIN)/poly_count: $(SRC)/usage/count_poly.o $(CORE) $(POLY_SURFACE) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/usage/count_poly.o $(CORE) $(POLY_SURFACE)

poly_print: $(BIN)/poly_print
$(BIN)/poly_print: $(SRC)/usage/print_poly.o $(CORE) $(POLY_SURFACE) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/usage/print_poly.o $(CORE) $(POLY_SURFACE)

$(SRC)/%.o: $(SRC)/%.c
	$(CC) $(CFLAGS) -I$(SRC) -c $< -o $@

clean:
	rm -f $(SRC)/*.o $(SRC)/core/*.o $(SRC)/rl0/*.o \
		$(SRC)/usage/*.o $(SRC)/throughput/*.o \
		$(BIN)/poly_count $(BIN)/poly_print $(BIN)/vcomp_count \
		$(BIN)/count_vcomp $(BIN)/vcomp_print $(BIN)/vcomp_parity \
		$(BIN)/imgtable $(BIN)/tile_to_imgtable \
		$(BIN)/rl0_generate $(BIN)/rl0_depict \
		$(BIN)/tiling_3x3_demo $(BIN)/tiling_4x4_demo \
		$(BIN)/hat_boundary_dfs_check \
		tests/check_rl0

tiling_3x3_demo: $(BIN)/tiling_3x3_demo
$(BIN)/tiling_3x3_demo: $(SRC)/usage/tiling_3x3_demo.o $(SRC)/core/dfs.o | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/usage/tiling_3x3_demo.o \
		$(SRC)/core/dfs.o

tiling_4x4_demo: $(BIN)/tiling_4x4_demo
$(BIN)/tiling_4x4_demo: $(SRC)/usage/tiling_4x4_demo.o $(SRC)/core/dfs.o | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/usage/tiling_4x4_demo.o \
		$(SRC)/core/dfs.o

hat_boundary_dfs_check: $(BIN)/hat_boundary_dfs_check
$(BIN)/hat_boundary_dfs_check: $(SRC)/usage/hat_boundary_dfs_check.o $(CORE) $(SRC)/core/dfs.o | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/usage/hat_boundary_dfs_check.o \
		$(CORE) $(SRC)/core/dfs.o

check_rl0: tests/check_rl0
tests/check_rl0: tests/check_rl0.c
	$(CC) -O2 -Wall -Wextra -std=c11 tests/check_rl0.c -o $@

vcomp_count: $(BIN)/vcomp_count
$(BIN)/vcomp_count: $(SRC)/usage/count_vcomp.o $(CORE) $(VCOMP_SURFACE) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/usage/count_vcomp.o $(CORE) $(VCOMP_SURFACE)

count_vcomp: $(BIN)/count_vcomp
$(BIN)/count_vcomp: $(BIN)/vcomp_count | $(BIN)
	cp $(BIN)/vcomp_count $(BIN)/count_vcomp

vcomp_parity: $(BIN)/vcomp_parity
$(BIN)/vcomp_parity: $(SRC)/usage/check_vcomp_parity.o $(CORE) $(VCOMP_SURFACE) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/usage/check_vcomp_parity.o $(CORE) $(VCOMP_SURFACE)

vcomp_print: $(BIN)/vcomp_print
$(BIN)/vcomp_print: $(SRC)/usage/print_vcomp.o $(CORE) $(VCOMP_SURFACE) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/usage/print_vcomp.o $(CORE) $(VCOMP_SURFACE)

imgtable: $(BIN)/imgtable
$(BIN)/imgtable: $(SRC)/usage/imgtable.o | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/usage/imgtable.o -lm

tile_to_imgtable: $(BIN)/tile_to_imgtable
$(BIN)/tile_to_imgtable: $(SRC)/usage/tile_to_imgtable.o $(SRC)/core/tile.o \
	$(SRC)/core/cycle.o $(SRC)/core/tetrille.o $(SRC)/core/lattice.o
	$(CC) $(CFLAGS) -o $@ $(SRC)/usage/tile_to_imgtable.o \
		$(SRC)/core/tile.o $(SRC)/core/cycle.o \
		$(SRC)/core/tetrille.o $(SRC)/core/lattice.o

rl0_generate: $(BIN)/rl0_generate
$(BIN)/rl0_generate: $(SRC)/rl0/generate_rl0.o $(CORE) $(VCOMP_SURFACE) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/rl0/generate_rl0.o $(CORE) $(VCOMP_SURFACE)

rl0_depict: $(BIN)/rl0_depict
$(BIN)/rl0_depict: $(SRC)/usage/depict_rl0.o $(CORE) $(SRC)/throughput/vcomp.o | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/usage/depict_rl0.o $(CORE) $(SRC)/throughput/vcomp.o

rl0_stats: $(BIN)/rl0_stats
$(BIN)/rl0_stats: $(SRC)/usage/rl0_stats.o $(CORE) $(SRC)/throughput/vcomp.o | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/usage/rl0_stats.o $(CORE) $(SRC)/throughput/vcomp.o

test: all
	./tests/smoke.sh

rl0_data: rl0_generate
	$(BIN)/rl0_generate tiles/hat.tile data/rl0/completions.dat

rl0_svg: rl0_data rl0_depict imgtable
	$(BIN)/rl0_depict --data data/rl0/completions.dat --grouped | \
		$(BIN)/imgtable > img/rl0_all.svg

$(BIN):
	mkdir -p $(BIN)

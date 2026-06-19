cmake_minimum_required(VERSION 3.16)
project(PolyformTown C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

add_compile_options(-O3 -Wall -Wextra)

set(SRC ${CMAKE_SOURCE_DIR}/src)
set(MY_HAT ${CMAKE_SOURCE_DIR}/mystuff)
set(RESTART ${CMAKE_SOURCE_DIR}/restart)

include_directories(${SRC})

# ---------------------------------------------------------------------------
# Shared object libraries — compiled once, reused across targets
# ---------------------------------------------------------------------------

add_library(core OBJECT
    ${SRC}/core/cycle.c
    ${SRC}/core/tile.c
    ${SRC}/core/hash.c
    ${SRC}/core/vec.c
    ${SRC}/core/attach.c
    ${SRC}/core/tetrille.c
    ${SRC}/core/lattice.c
    ${SRC}/core/boundary.c
    ${SRC}/core/numerics.c
    ${SRC}/core/term_color.c
)

add_library(vcomp_obj        OBJECT ${SRC}/throughput/vcomp.c)
add_library(vcomp_pipeline_obj OBJECT ${SRC}/throughput/vcomp_pipeline.c)
add_library(poly_pipeline_obj  OBJECT ${SRC}/throughput/poly_pipeline.c)

add_library(rl0_surface OBJECT
    ${SRC}/rl0/forget_map.c
    ${SRC}/rl0/boundary0.c
    ${SRC}/rl0/vcomp0.c
    ${SRC}/rl0/attach0.c
)

add_library(bcomp1_obj OBJECT ${SRC}/rl1/bcomp1.c)

# Convenience variables for groups of object files
set(CORE_OBJS         $<TARGET_OBJECTS:core>)
set(VCOMP_OBJS        $<TARGET_OBJECTS:vcomp_obj>)
set(VCOMP_SURFACE_OBJS $<TARGET_OBJECTS:vcomp_obj> $<TARGET_OBJECTS:vcomp_pipeline_obj>)
set(POLY_SURFACE_OBJS  $<TARGET_OBJECTS:vcomp_obj> $<TARGET_OBJECTS:poly_pipeline_obj>)
set(RL0_SURFACE_OBJS  $<TARGET_OBJECTS:rl0_surface>)
set(BCOMP1_OBJS       $<TARGET_OBJECTS:bcomp1_obj>)

# ---------------------------------------------------------------------------
# Core tools
# ---------------------------------------------------------------------------

add_executable(poly_count  ${SRC}/usage/count_poly.c      ${CORE_OBJS} ${POLY_SURFACE_OBJS})
add_executable(poly_print  ${SRC}/usage/print_poly.c       ${CORE_OBJS} ${POLY_SURFACE_OBJS})
add_executable(vcomp_count ${SRC}/usage/count_vcomp.c      ${CORE_OBJS} ${VCOMP_SURFACE_OBJS})
add_executable(count_vcomp ${SRC}/usage/count_vcomp.c      ${CORE_OBJS} ${VCOMP_SURFACE_OBJS})
add_executable(vcomp_print ${SRC}/usage/print_vcomp.c      ${CORE_OBJS} ${VCOMP_SURFACE_OBJS})
add_executable(vcomp_parity ${SRC}/usage/check_vcomp_parity.c ${CORE_OBJS} ${VCOMP_SURFACE_OBJS})

# ---------------------------------------------------------------------------
# Run levels
# ---------------------------------------------------------------------------

add_executable(rl0_generate ${SRC}/rl0/generate_rl0.c
    ${CORE_OBJS} ${VCOMP_SURFACE_OBJS})

add_executable(rl0_refine ${SRC}/rl0/rl0_refine.c
    ${RL0_SURFACE_OBJS} ${CORE_OBJS} ${VCOMP_SURFACE_OBJS})

add_executable(rl1_generate_naive ${SRC}/rl1/generate_rl1_naive.c
    ${CORE_OBJS} ${VCOMP_SURFACE_OBJS})

add_executable(rl1_generate ${SRC}/rl1/generate_rl1.c
    ${RL0_SURFACE_OBJS} ${CORE_OBJS} ${VCOMP_SURFACE_OBJS})

add_executable(rl1_refine ${SRC}/rl1/refine1.c
    ${BCOMP1_OBJS} ${RL0_SURFACE_OBJS} ${CORE_OBJS} ${VCOMP_SURFACE_OBJS})

add_executable(rl2_generate ${SRC}/rl2/generate_rl2.c
    ${BCOMP1_OBJS} ${RL0_SURFACE_OBJS} ${CORE_OBJS} ${VCOMP_SURFACE_OBJS})

add_executable(rl3_generate ${SRC}/rl3/generate_rl3.c
    ${BCOMP1_OBJS} ${RL0_SURFACE_OBJS} ${CORE_OBJS} ${VCOMP_SURFACE_OBJS})

add_executable(rl4_generate ${SRC}/rl4/generate_rl4.c
    ${BCOMP1_OBJS} ${RL0_SURFACE_OBJS} ${CORE_OBJS} ${VCOMP_SURFACE_OBJS})

add_executable(supertile_depict ${SRC}/rl4/refine4.c
    ${BCOMP1_OBJS} ${RL0_SURFACE_OBJS} ${CORE_OBJS} ${VCOMP_SURFACE_OBJS})
target_link_libraries(supertile_depict m)

add_executable(rl4_supertile_candidates ${SRC}/rl4/supertile_candidates.c)

add_executable(rl5_refine ${SRC}/rl5/refine5.c)

add_executable(pathological_item6 ${SRC}/usage/pathological_item6.c
    ${BCOMP1_OBJS} ${RL0_SURFACE_OBJS} ${CORE_OBJS} ${VCOMP_SURFACE_OBJS})

add_executable(rl5_sat
    ${SRC}/rl5/sat5.c
    ${SRC}/rl5/model5.c)

add_executable(rl5_hexagons ${SRC}/rl4/hexagons.c
    ${RL0_SURFACE_OBJS} ${CORE_OBJS} ${VCOMP_SURFACE_OBJS})

add_executable(rl5_supertile_hexagons ${SRC}/rl4/refine4.c
    ${BCOMP1_OBJS} ${RL0_SURFACE_OBJS} ${CORE_OBJS} ${VCOMP_SURFACE_OBJS})
target_link_libraries(rl5_supertile_hexagons m)

add_executable(rl5_stats
    ${SRC}/rl5/stats5.c
    ${SRC}/rl5/model5.c
    ${SRC}/rl5/attach5.c
    ${SRC}/rl5/vcomp5.c
    ${SRC}/rl5/bcomp5.c
    ${SRC}/rl5/boundary5.c
    ${SRC}/core/term_color.c)

add_executable(rl6_reduce
    ${SRC}/rl6/reduce6.c
    ${SRC}/rl5/model5.c
    ${SRC}/rl5/attach5.c
    ${SRC}/rl5/vcomp5.c
    ${SRC}/rl5/bcomp5.c
    ${SRC}/rl5/boundary5.c)

add_executable(rl6_refine
    ${SRC}/rl6/reduce6.c
    ${SRC}/rl5/model5.c
    ${SRC}/rl5/attach5.c
    ${SRC}/rl5/vcomp5.c
    ${SRC}/rl5/bcomp5.c
    ${SRC}/rl5/boundary5.c)

add_executable(rl6_generate ${SRC}/rl6/generate6.c)

add_executable(rl6_print_conversion ${SRC}/usage/rl6/generate_print_conversion.c)

# ---------------------------------------------------------------------------
# RL7 tools
# ---------------------------------------------------------------------------

add_executable(hce
    ${SRC}/usage/rl7/hce.c
    ${SRC}/rl7/boundary7.c
    ${SRC}/rl7/comp7.c
    ${SRC}/rl7/evo7.c
    ${SRC}/usage/print7.c)

add_executable(rephex ${SRC}/usage/rl7/rephex.c)

add_executable(rephex_print
    ${SRC}/usage/rl7/rephex_print.c
    ${SRC}/usage/rl7/rephex_hat.c
    ${SRC}/rl7/inflation7.c)
target_link_libraries(rephex_print m)

add_executable(rephex_hacked ${SRC}/usage/rl7/rephex_hacked.c)
target_link_libraries(rephex_hacked m)

# ---------------------------------------------------------------------------
# Depict / visualisation tools
# ---------------------------------------------------------------------------

add_executable(imgtable ${SRC}/usage/imgtable.c)
target_link_libraries(imgtable m)

add_executable(tile_to_imgtable
    ${SRC}/usage/tile_to_imgtable.c
    ${SRC}/core/tile.c
    ${SRC}/core/cycle.c
    ${SRC}/core/tetrille.c
    ${SRC}/core/lattice.c)

add_executable(rl0_depict ${SRC}/usage/depict_rl0.c
    ${CORE_OBJS} ${VCOMP_OBJS})

add_executable(rl1_depict ${SRC}/usage/depict_rl1.c
    ${RL0_SURFACE_OBJS} ${CORE_OBJS} ${VCOMP_OBJS})

add_executable(depict_search0_graph ${SRC}/usage/depict_search0_graph.c)
target_link_libraries(depict_search0_graph m)

# ---------------------------------------------------------------------------
# Extra / historic tools
# ---------------------------------------------------------------------------

add_executable(rl1_hexagons ${SRC}/rl4/hexagons.c
    ${RL0_SURFACE_OBJS} ${CORE_OBJS} ${VCOMP_SURFACE_OBJS})

add_executable(rl1_search ${SRC}/rl1/search1.c
    ${BCOMP1_OBJS} ${RL0_SURFACE_OBJS} ${CORE_OBJS} ${VCOMP_SURFACE_OBJS})

add_executable(rl0_stats ${SRC}/usage/rl0_stats.c
    ${CORE_OBJS} ${VCOMP_OBJS})

add_executable(hat_boundary_dfs_check ${SRC}/usage/hat_boundary_dfs_check.c
    ${CORE_OBJS} ${SRC}/core/dfs.c)

add_executable(check_indexed_variants ${SRC}/usage/check_indexed_variants.c
    ${SRC}/core/tile.c
    ${SRC}/core/cycle.c
    ${SRC}/core/tetrille.c
    ${SRC}/core/lattice.c)

add_executable(check_rl1_tile_order ${SRC}/usage/check_rl1_tile_order.c
    ${BCOMP1_OBJS} ${RL0_SURFACE_OBJS} ${CORE_OBJS} ${VCOMP_SURFACE_OBJS})

add_executable(gen_graph ${SRC}/meta/gen_graph.c)

add_executable(tiling_3x3_demo ${SRC}/usage/tiling_3x3_demo.c ${SRC}/core/dfs.c)
add_executable(tiling_4x4_demo ${SRC}/usage/tiling_4x4_demo.c ${SRC}/core/dfs.c)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

add_executable(check_rl0 tests/check_rl0.c)
target_compile_options(check_rl0 PRIVATE -O2)

add_executable(check_rl0_forget_map tests/check_rl0_forget_map.c
    ${RL0_SURFACE_OBJS} ${CORE_OBJS} ${VCOMP_SURFACE_OBJS})

add_executable(check_rl0_incremental_graph tests/check_rl0_incremental_graph.c
    ${RL0_SURFACE_OBJS} ${CORE_OBJS} ${VCOMP_SURFACE_OBJS})

add_executable(check_rl1_naive_filtration tests/check_rl1_naive_filtration.c
    ${RL0_SURFACE_OBJS} ${CORE_OBJS} ${VCOMP_SURFACE_OBJS})

# ---------------------------------------------------------------------------
# MyHatTileStuff — personal experiments
# ---------------------------------------------------------------------------

add_executable(converter ${MY_HAT}/converter.c ${CORE_OBJS})
target_link_libraries(converter m)

add_executable(csv2svg ${MY_HAT}/csv2svg.c)
target_link_libraries(csv2svg m)

add_executable(rotsym ${MY_HAT}/rotsym.c)
target_link_libraries(rotsym m)

add_executable(hat_three_no_rotation ${MY_HAT}/hat_three_no_rotation.c ${CORE_OBJS})
add_executable(hat_four_no_rotation  ${MY_HAT}/hat_four_no_rotation.c  ${CORE_OBJS})

add_executable(surrounding  ${MY_HAT}/surrounding.c  ${CORE_OBJS})
target_link_libraries(surrounding m)

add_executable(surrounding2 ${MY_HAT}/surrounding2.c ${CORE_OBJS})
target_link_libraries(surrounding2 m)

add_executable(centers ${MY_HAT}/centers.c ${CORE_OBJS})
target_link_libraries(centers m)

add_executable(composing ${MY_HAT}/composing.c ${CORE_OBJS})
target_link_libraries(composing m)

add_executable(learning_by_doing ${RESTART}/learning_by_doing.c ${CORE_OBJS})
target_link_libraries(learning_by_doing m)

add_executable(growth ${MY_HAT}/growth.c
    ${BCOMP1_OBJS} ${RL0_SURFACE_OBJS} ${CORE_OBJS} ${VCOMP_SURFACE_OBJS})
target_link_libraries(growth m)

add_executable(grow_symmetric ${MY_HAT}/grow_symmetric.c
    ${BCOMP1_OBJS} ${RL0_SURFACE_OBJS} ${CORE_OBJS} ${VCOMP_SURFACE_OBJS})
target_link_libraries(grow_symmetric m)

add_executable(grow_sym_alt ${MY_HAT}/grow_sym_alt.c
    ${BCOMP1_OBJS} ${RL0_SURFACE_OBJS} ${CORE_OBJS} ${VCOMP_SURFACE_OBJS})
target_link_libraries(grow_sym_alt m)

add_executable(grow_symmetric2 ${MY_HAT}/grow_symmetric2.c
    ${BCOMP1_OBJS} ${RL0_SURFACE_OBJS} ${CORE_OBJS} ${VCOMP_SURFACE_OBJS})
target_link_libraries(grow_symmetric2 m)

add_executable(h2_cluster ${MY_HAT}/h2_cluster.c
    ${BCOMP1_OBJS} ${RL0_SURFACE_OBJS} ${CORE_OBJS} ${VCOMP_SURFACE_OBJS})
target_link_libraries(h2_cluster m)

add_executable(h3_cluster ${MY_HAT}/h3_cluster.c
    ${BCOMP1_OBJS} ${RL0_SURFACE_OBJS} ${CORE_OBJS} ${VCOMP_SURFACE_OBJS})
target_link_libraries(h3_cluster m)

add_executable(h4_cluster ${MY_HAT}/h4_cluster.c
    ${BCOMP1_OBJS} ${RL0_SURFACE_OBJS} ${CORE_OBJS} ${VCOMP_SURFACE_OBJS})
target_link_libraries(h4_cluster m)

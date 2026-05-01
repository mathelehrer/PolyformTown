#!/usr/bin/awk -f

# RL0 parity validator (awk)
# Usage: tests/validate_rl0_parity.sh [data/rl0/completions.dat]

function fail(step, msg) {
  print "FAIL [" step "] " msg > "/dev/stderr"
  exit 1
}

function trim(s) {
  gsub(/^[[:space:]]+|[[:space:]]+$/, "", s)
  return s
}

function reset_record_state() {
  have_boundary = 0
  have_tiles = 0
  have_parities = 0
  have_indices = 0
  have_center = 0

  boundary_text = ""
  tiles_text = ""
  parities_csv = ""
  indices_csv = ""
  center_vertex = ""
}

function add_vertex_tokens(text, token_counts,   rest, token) {
  rest = text
  while (match(rest, /\([^)]+\)/)) {
    token = substr(rest, RSTART, RLENGTH)
    token_counts[token]++
    rest = substr(rest, RSTART + RLENGTH)
  }
}

function extract_cycle_vertices(cycle_text, vertices,   rest, n) {
  delete vertices
  rest = cycle_text
  n = 0
  while (match(rest, /\([^)]+\)/)) {
    vertices[++n] = substr(rest, RSTART, RLENGTH)
    rest = substr(rest, RSTART + RLENGTH)
  }
  return n
}

function check_center_index_by_parity(record_id, parity_values, index_values, parity_count,   scan, char_i, ch, bracket_depth, tile_start, tile_text, tile_no, vertex_count, tile_vertices, index0, parity) {
  scan = tiles_text
  bracket_depth = 0
  tile_start = 0
  tile_no = 0

  for (char_i = 1; char_i <= length(scan); char_i++) {
    ch = substr(scan, char_i, 1)

    if (ch == "[") {
      bracket_depth++
      if (bracket_depth == 2) tile_start = char_i
      continue
    }

    if (ch != "]") continue

    if (bracket_depth == 2) {
      tile_text = substr(scan, tile_start, char_i - tile_start + 1)
      tile_no++

      if (tile_no > parity_count)
        fail("STEP4", "more tiles than parity values in record " record_id)

      vertex_count = extract_cycle_vertices(tile_text, tile_vertices)
      index0 = index_values[tile_no] + 0
      parity = parity_values[tile_no] + 0

      if (index0 < 0 || index0 >= vertex_count)
        fail("STEP4", "index out of range in record " record_id ", tile " tile_no)

      if (parity == 1) {
        if (tile_vertices[index0 + 1] != center_vertex)
          fail("STEP4", "+1 center/index mismatch in record " record_id ", tile " tile_no)
      } else if (parity == -1) {
        if (tile_vertices[vertex_count - index0] != center_vertex)
          fail("STEP4", "-1 center/index mismatch in record " record_id ", tile " tile_no)
      } else {
        fail("STEP3", "parity must be +/-1 in record " record_id ", tile " tile_no)
      }
    }

    bracket_depth--
  }

  if (tile_no != parity_count)
    fail("STEP3", "tile/parity count mismatch in record " record_id)
}

function validate_current_record(record_id,   i, n_parities, n_indices, parity_values, index_values, token, token_counts) {
  if (record_id <= 0) return

  if (!have_boundary || !have_tiles || !have_parities || !have_indices || !have_center)
    fail("STEP2", "missing required field(s) in record " record_id)

  n_parities = split(parities_csv, parity_values, ",")
  n_indices  = split(indices_csv, index_values, ",")

  if (n_parities != n_indices)
    fail("STEP3", "parity/index length mismatch in record " record_id)

  for (i = 1; i <= n_parities; i++) {
    if (parity_values[i] != "1" && parity_values[i] != "-1")
      fail("STEP3", "parity must be +/-1 in record " record_id)
  }

  delete token_counts
  add_vertex_tokens(boundary_text, token_counts)
  add_vertex_tokens(tiles_text, token_counts)

  for (token in token_counts) {
    if (token_counts[token] <= 1)
      fail("STEP5", "vertex without duplicate in merged boundary+tiles in record " record_id ": " token)
  }

  check_center_index_by_parity(record_id, parity_values, index_values, n_parities)
}

function parse_record_line(line) {
  if (line ~ /^boundary:/) {
    have_boundary = 1
    boundary_text = trim(substr(line, 10))
    return
  }

  # Backward compatibility with old field name.
  if (line ~ /^canonical_boundary:/) {
    if (!have_boundary) {
      have_boundary = 1
      boundary_text = trim(substr(line, 20))
    }
    return
  }

  if (line ~ /^tiles:/) {
    have_tiles = 1
    tiles_text = trim(substr(line, 7))
    return
  }

  if (line ~ /^parities:/) {
    have_parities = 1
    parities_csv = substr(line, 10)
    gsub(/[\[\] ]/, "", parities_csv)
    return
  }

  if (line ~ /^indices:/) {
    have_indices = 1
    indices_csv = substr(line, 9)
    gsub(/[\[\] ]/, "", indices_csv)
    return
  }

  if (line ~ /^center:/) {
    have_center = 1
    center_vertex = trim(substr(line, 8))
    gsub(/[[:space:]]/, "", center_vertex)
    return
  }
}

BEGIN {
  data_file = (ARGC > 1 ? ARGV[1] : "data/rl0/completions.dat")

  # STEP1: global record-count sanity
  record_count = 0
  while ((getline line < data_file) > 0) {
    if (line ~ /^---\[/) record_count++
  }
  close(data_file)

  if (record_count != 44)
    fail("STEP1", "expected 44 records, got " record_count)

  # STEP2..STEP5: parse records and validate
  current_record_id = 0
  reset_record_state()

  while ((getline line < data_file) > 0) {
    if (line ~ /^---\[/) {
      validate_current_record(current_record_id)
      current_record_id++
      reset_record_state()
      continue
    }
    parse_record_line(line)
  }
  close(data_file)

  validate_current_record(current_record_id)

  if (current_record_id == 0)
    fail("STEP1", "no records found")

  print "ok"
}

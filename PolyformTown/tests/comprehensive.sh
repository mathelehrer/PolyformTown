#!/usr/bin/env bash
set -u -o pipefail

i=1
fails=0

check_eq() {
  if [[ "$1" != "$2" ]]; then
    echo "FAIL [$i]: expected $2 got $1"
    fails=$((fails+1))
  else
    echo "PASS [$i]"
  fi
  i=$((i+1))
}

check_cmd_eq() {
  local expected="$1"
  local actual
  shift
  actual=$("$@")
  local status=$?
  if [[ "$status" -ne 0 ]]; then
    echo "FAIL [$i]: command exited $status: $*"
    fails=$((fails+1))
    i=$((i+1))
    return
  fi
  check_eq "$actual" "$expected"
}

get_count() {
  tail -n1 | awk '{print $2}' | sed 's/count=//'
}

count_holes() {
  awk '/^\[ *1/ {c++} END{print c+0}'
}

count_vcomp_aggregate_holes() {
  awk 'prev == "Aggregate" && /^\[ *1/ {c++} {prev=$0} END{print c+0}'
}

export -f get_count count_holes count_vcomp_aggregate_holes

# ---- POLY COUNT ----
check_cmd_eq "4655" bash -c './poly_count 10 tiles/monomino.tile | get_count'
check_cmd_eq "2227" bash -c './poly_count 5  tiles/domino.tile   | get_count'
check_cmd_eq "7142" bash -c './poly_count 4  tiles/chair.tile    | get_count'
check_cmd_eq "3334" bash -c './poly_count 12 tiles/triangle.tile | get_count'
check_cmd_eq "1448" bash -c './poly_count 8  tiles/hexagon.tile  | get_count'
check_cmd_eq "1552" bash -c './poly_count 4  tiles/hh.tile       | get_count'
check_cmd_eq "2917" bash -c './poly_count 9  tiles/kite.tile     | get_count'
check_cmd_eq "459"  bash -c './poly_count 3  tiles/hat.tile      | get_count'

# ---- POLY PRINT (holes only) ----
check_cmd_eq "195" bash -c './poly_print 10 tiles/monomino.tile | count_holes'
check_cmd_eq "69"  bash -c './poly_print 5  tiles/domino.tile   | count_holes'
check_cmd_eq "634" bash -c './poly_print 4  tiles/chair.tile    | count_holes'
check_cmd_eq "108" bash -c './poly_print 12 tiles/triangle.tile | count_holes'
check_cmd_eq "13"  bash -c './poly_print 8  tiles/hexagon.tile  | count_holes'
check_cmd_eq "54"  bash -c './poly_print 4  tiles/hh.tile       | count_holes'
check_cmd_eq "141" bash -c './poly_print 9  tiles/kite.tile     | count_holes'
check_cmd_eq "94"  bash -c './poly_print 3  tiles/hat.tile      | count_holes'

# ---- POLY PRINT (hole removal) ----
check_cmd_eq "246" bash -c './poly_print 3 tiles/chair.tile --live-only | wc -l'
check_cmd_eq "63"  bash -c './poly_print 2 tiles/tetL.tile  --live-only | wc -l'
check_cmd_eq "16"  bash -c './poly_print 2 tiles/hat.tile   --live-only | wc -l'

# ---- VCOMP COUNT ----
check_cmd_eq "30" bash -c './vcomp_count 6 tiles/monomino.tile | get_count'
check_cmd_eq "29" bash -c './vcomp_count 3 tiles/domino.tile   | get_count'
check_cmd_eq "25" bash -c './vcomp_count 2 tiles/chair.tile    | get_count'
check_cmd_eq "21" bash -c './vcomp_count 5 tiles/triangle.tile | get_count'
check_cmd_eq "13" bash -c './vcomp_count 7 tiles/hexagon.tile  | get_count'
check_cmd_eq "27" bash -c './vcomp_count 1 tiles/hh.tile       | get_count'
check_cmd_eq "85" bash -c './vcomp_count 6 tiles/kite.tile     | get_count'
check_cmd_eq "13" bash -c './vcomp_count 4 tiles/hat.tile      | get_count'

# ---- VCOMP TRACK PARITY ----
check_cmd_eq "PASS parity tiles/domino.tile
PASS parity tiles/chair.tile
PASS parity tiles/hat.tile" ./vcomp_parity

# ---- VCOMP PRINT (holes only) ----
check_cmd_eq "0" bash -c './vcomp_print 6 tiles/monomino.tile | count_vcomp_aggregate_holes'
check_cmd_eq "0" bash -c './vcomp_print 3 tiles/domino.tile   | count_vcomp_aggregate_holes'
check_cmd_eq "0" bash -c './vcomp_print 2 tiles/chair.tile    | count_vcomp_aggregate_holes'
check_cmd_eq "0" bash -c './vcomp_print 5 tiles/triangle.tile | count_vcomp_aggregate_holes'
check_cmd_eq "0" bash -c './vcomp_print 7 tiles/hexagon.tile  | count_vcomp_aggregate_holes'
check_cmd_eq "0" bash -c './vcomp_print 1 tiles/hh.tile       | count_vcomp_aggregate_holes'
check_cmd_eq "0" bash -c './vcomp_print 6 tiles/kite.tile     | count_vcomp_aggregate_holes'
check_cmd_eq "7" bash -c './vcomp_print 4 tiles/hat.tile      | count_vcomp_aggregate_holes'

if [[ "$fails" -gt 0 ]]; then
  echo "TOTAL FAILS: $fails"
  exit 1
fi
echo 0

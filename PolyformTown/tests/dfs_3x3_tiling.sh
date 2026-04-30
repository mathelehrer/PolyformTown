#!/usr/bin/env bash
set -euo pipefail

out="$(./tiling_3x3_demo)"
[[ "$out" == placements=22* ]]
[[ "$out" == *"solutions=10"* ]]
[[ "$out" == *"canonical=2"* ]]

out_mrv="$(./tiling_3x3_demo --order mrv)"
[[ "$out_mrv" == *"order=mrv"* ]]

trace_file="/tmp/tiling_3x3_trace.tsv"
./tiling_3x3_demo --trace "$trace_file" >/tmp/tiling_3x3_trace.out

[[ -f "$trace_file" ]]
head -n 1 "$trace_file" | grep -q '^ev'

echo 0

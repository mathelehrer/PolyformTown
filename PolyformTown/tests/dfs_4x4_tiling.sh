#!/usr/bin/env bash
set -euo pipefail

out="$(./tiling_4x4_demo)"
[[ "$out" == *"order=index"* ]]
[[ "$out" == *"order=rare"* ]]
[[ "$out" == *"order=common"* ]]
[[ "$out" == *"order=mrv"* ]]
[[ "$out" == *"full=1111111111111111"* ]]

echo 0

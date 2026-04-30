#!/usr/bin/env bash
set -euo pipefail

out="$(./tiling_4x4_demo)"
[[ "$out" == *"order=index"* ]]
[[ "$out" == *"order=rare"* ]]
[[ "$out" == *"order=common"* ]]

echo 0

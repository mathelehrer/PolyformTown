#!/usr/bin/env bash
set -euo pipefail

out=${1:-img/rl7/inflation}
root=$(cd "$(dirname "$0")/../../.." && pwd)
bin="$root/bin/rephex_print"
if [[ "$out" = /* ]]; then
    out_abs="$out"
else
    out_abs="$root/$out"
fi
term_abs="$out_abs/terminal"
mkdir -p "$out_abs" "$term_abs"

make -C "$root" rephex_print >/dev/null
REPHEX_NO_OPEN=1 "$bin" --write-axioms "$root/data/rl7/inflation/axioms.dat" >/dev/null

# Optional review artifact: the primary interface is one axiom at a time.
inits=(D H DH B3 L3 F)
for level in 0 1 2; do
    for palette in ordinary tree; do
        for init in "${inits[@]}"; do
            tag="$init"
            if [[ "$palette" == ordinary ]]; then
                pal=()
            else
                pal=(--palette tree)
            fi
            REPHEX_NO_OPEN=1 "$bin" "$init" "$level" "${pal[@]}" \
                --svg --no-color >/dev/null
            cp "$root/img/rl7/rephex/current.svg" "$out_abs/${tag}_inflation${level}_${palette}.svg"
            REPHEX_NO_OPEN=1 "$bin" "$init" "$level" "${pal[@]}" --color \
                > "$term_abs/${tag}_inflation${level}_${palette}.ans"
            REPHEX_NO_OPEN=1 "$bin" "$init" "$level" "${pal[@]}" --no-color \
                > "$term_abs/${tag}_inflation${level}_${palette}.txt"
        done
    done
done

python3 "$root/src/usage/rl7/assemble_inflation_catalogue.py" "$out_abs" ordinary >/dev/null
python3 "$root/src/usage/rl7/assemble_inflation_catalogue.py" "$out_abs" tree >/dev/null

if python3 -c 'import cairosvg' >/dev/null 2>&1; then
    python3 - "$out_abs" <<'PY'
from pathlib import Path
import sys
import cairosvg
out = Path(sys.argv[1])
for src in sorted(out.glob('*.svg')):
    cairosvg.svg2png(url=str(src), write_to=str(src.with_suffix('.png')))
PY
fi
printf 'generated C-derived SVG/PNG catalogue and ANSI/text diagrams in %s\n' "$out_abs"

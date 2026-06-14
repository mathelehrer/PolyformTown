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
mkdir -p "$out_abs" "$term_abs" "$root/data/rl7/inflation"

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

# Keep the source tree C/shell-only.  If a system SVG converter is present,
# produce PNG siblings; otherwise leave the canonical SVGs in place.
if command -v rsvg-convert >/dev/null 2>&1; then
    for src in "$out_abs"/*.svg; do
        rsvg-convert "$src" -o "${src%.svg}.png"
    done
elif command -v inkscape >/dev/null 2>&1; then
    for src in "$out_abs"/*.svg; do
        inkscape "$src" --export-type=png --export-filename="${src%.svg}.png" >/dev/null 2>&1 || true
    done
fi
printf 'generated C-derived SVGs and ANSI/text diagrams in %s\n' "$out_abs"

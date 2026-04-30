#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

make rl0_data >/dev/null

tmpd="$(mktemp -d)"
trap 'rm -rf "$tmpd"' EXIT

rl0_keys="$tmpd/rl0.keys"
sift_keys="$tmpd/sift.keys"

awk '
function trim(s){gsub(/^[[:space:]]+|[[:space:]]+$/, "", s); return s}
function canonb(s){
  gsub(/^\[\[|\]\]$/, "", s)
  gsub(/\),\(/, ")|(", s)
  gsub(/\(/, "", s)
  gsub(/\)/, "", s)
  n=split(s,a,"|")
  out=""
  for(i=1;i<=n;i++){
    gsub(/,/, " ", a[i])
    gsub(/[[:space:]]+/, " ", a[i]); a[i]=trim(a[i])
    out=out (i>1?",":"") "(" a[i] ")"
  }
  return "[" out "]"
}
/^center:/ {center=substr($0,8); gsub(/[()]/,"",center); gsub(/,/," ",center); gsub(/[[:space:]]+/," ",center); center=trim(center)}
/^canonical_boundary:/ {
  b=substr($0,index($0,":")+1)
  b=trim(b)
  print center "|" canonb(b)
}
' levelData/rl0/completions.dat | sort -u > "$rl0_keys"

for n in $(seq 1 10); do
  ./vcomp_print "$n" tiles/hat.tile --live-only
done > "$tmpd/vcomp.out"

awk '
function trim(s){gsub(/^[[:space:]]+|[[:space:]]+$/, "", s); return s}
function normv(s){
  gsub(/[()]/,"",s); gsub(/,/," ",s); gsub(/[[:space:]]+/," ",s)
  return trim(s)
}
function canon_cycle_csv(seg,   n,i,v,out){
  n=split(seg,a,",")
  out=""
  for(i=1;i<=n;i++){
    v=trim(a[i])
    out=out (i>1?",":"") "(" v ")"
  }
  return "[" out "]"
}
function begin_item(){
  in_item=1; have_agg=0; tile_n=0; hidden_n=0; in_tiles=0; in_hidden=0
}
function flush_item(   i,j,c,v,ok,k){
  if(!in_item) return
  if(!have_agg){
    printf("ERROR: missing Aggregate boundary in item\n") > "/dev/stderr"; exit 2
  }
  if(tile_n==0){
    printf("ERROR: missing Tiles data in item\n") > "/dev/stderr"; exit 2
  }
  if(hidden_n==0){
    printf("ERROR: missing Hidden data in item\n") > "/dev/stderr"; exit 2
  }
  for(i=1;i<=hidden_n;i++){
    c=hidden[i]; ok=1
    for(j=1;j<=tile_n;j++){
      if(index(tile[j], "|" c "|")==0){ok=0; break}
    }
    if(ok){print c "|" agg_boundary}
  }
  in_item=0
}
/^\[[0-9]+\]$/ {flush_item(); begin_item(); next}
/^Aggregate$/ {in_tiles=0; in_hidden=0; expect_agg=1; next}
expect_agg && /^\[/ {
  b=$0
  sub(/^.*\| \(/, "", b)
  sub(/\) \]$/, "", b)
  if (b==$0) {
    printf("ERROR: cannot parse Aggregate boundary format\n") > "/dev/stderr"; exit 2
  }
  agg_boundary=canon_cycle_csv(b); have_agg=1; expect_agg=0; next
}
/^Tiles$/ {in_tiles=1; in_hidden=0; next}
/^Hidden$/ {in_tiles=0; in_hidden=1; next}
in_tiles && /^\[/ {
  seg=$0
  sub(/^.*\| \(/, "", seg)
  sub(/\) \]$/, "", seg)
  if (seg==$0) {
    printf("ERROR: cannot parse Tiles boundary format\n") > "/dev/stderr"; exit 2
  }
  n=split(seg,a,",")
  line="|"
  for(i=1;i<=n;i++){v=trim(a[i]); line=line v "|"}
  tile[++tile_n]=line
  next
}
in_hidden {
  line=$0
  while(match(line,/\([0-9-]+,[0-9-]+,[0-9-]+\)/)){
    c=substr(line,RSTART,RLENGTH)
    hidden[++hidden_n]=normv(c)
    line=substr(line,RSTART+RLENGTH)
  }
  next
}
END{flush_item()}
' "$tmpd/vcomp.out" | sort -u > "$sift_keys"

rl0_n="$(wc -l < "$rl0_keys" | tr -d ' ')"
sift_n="$(wc -l < "$sift_keys" | tr -d ' ')"
inter_n="$(comm -12 "$rl0_keys" "$sift_keys" | wc -l | tr -d ' ')"
missing_n="$(comm -23 "$rl0_keys" "$sift_keys" | wc -l | tr -d ' ')"
extra_n="$(comm -13 "$rl0_keys" "$sift_keys" | wc -l | tr -d ' ')"

echo "rl0_keys=$rl0_n"
echo "sift_keys=$sift_n"
echo "intersect=$inter_n"
echo "missing=$missing_n"
echo "extra=$extra_n"

if [ "$rl0_n" -ne 44 ]; then
  echo "FAIL: expected rl0_keys=44, got $rl0_n" >&2
  exit 1
fi
if [ "$missing_n" -gt 0 ] || [ "$extra_n" -gt 0 ]; then
  echo "FAIL: key mismatch (missing=$missing_n extra=$extra_n)" >&2
  exit 1
fi

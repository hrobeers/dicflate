#!/usr/bin/env bash
# Process-level benchmark for the dicflate tools.
# Reports the minimum of N runs, which approximates the noise-free cost,
# because one-shot timings are dominated by process startup and scheduler
# noise. Each run reopens its own input.
#
# Inputs default to /tmp/solved.loft and /tmp/unsolved.dict when present,
# and fall back to the repo's test data.
set -u
BD=$(cd "$(dirname "$0")/.." && pwd)
DD=${BD}/test/data
SRC=/tmp/solved.loft
DICT=/tmp/unsolved.dict
[ -r "$SRC" ] || { SRC=${DD}/generated.json; echo "(falling back to ${SRC})"; }
[ -r "$DICT" ] || { DICT=${DD}/generated.single.json; echo "(falling back to dict ${DICT})"; }

T=/tmp/bench-inputs
mkdir -p "$T"
cp "$SRC" "$T/in24k"
cat "$SRC" "$SRC" > "$T/in48k"
cat "$T/in48k" "$T/in48k" "$T/in48k" "$T/in48k" > "$T/in400k"
cat "$T/in400k" "$T/in400k" "$T/in400k" "$T/in400k" > "$T/in1m6"
cat "$T/in1m6" "$T/in1m6" "$T/in1m6" "$T/in1m6" > "$T/in3m"

minof() {  # minof <n> <stdin-file> <cmd...> -> min wall time in ms
  local n=$1; local inf=$2; shift 2
  local best=999999999 t0 t1 dt
  for ((i=0; i<n; i++)); do
    t0=${EPOCHREALTIME/./}
    "$@" < "$inf" > /dev/null 2> /dev/null
    t1=${EPOCHREALTIME/./}
    dt=$((t1 - t0))
    (( dt < best )) && best=$dt
  done
  awk -v b="$best" 'BEGIN{printf "%.2f", b/1000}'
}

echo "=== fixed overhead: startup + dict load (min of 200, ms) ==="
printf '%-26s %s\n' "dicflate empty"        "$(minof 200 /dev/null $BD/dicflate -r)"
printf '%-26s %s\n' "dicflate empty+dict"   "$(minof 200 /dev/null $BD/dicflate -r -d $DICT)"
printf '%-26s %s\n' "dicstd empty"          "$(minof 200 /dev/null $BD/dicstd -r)"
printf '%-26s %s\n' "dicstd empty+dict"     "$(minof 200 /dev/null $BD/dicstd -r -d $DICT)"
printf '%-26s %s\n' "diclz4 empty"          "$(minof 200 /dev/null $BD/diclz4 -r)"
printf '%-26s %s\n' "diclz4 empty+dict"     "$(minof 200 /dev/null $BD/diclz4 -r -d $DICT)"

echo
echo "=== full pipeline, default levels, raw (-r), dict (min of N, ms) ==="
echo "    (dicflate/dicstd/diclz4 defaults: zlib 6 / zstd 3 / lz4 0)"
printf '%-8s %10s %9s %9s %9s %6s\n' "input" "bytes" "dicflate" "dicstd" "diclz4" "runs"
for f in in24k in48k in400k in1m6 in3m; do
  case $f in
    in24k)  n=200;; in48k) n=150;; in400k) n=50;; in1m6) n=20;; in3m) n=8;;
  esac
  bytes=$(wc -c < $T/$f)
  a=$(minof $n $T/$f $BD/dicflate -r -d $DICT)
  b=$(minof $n $T/$f $BD/dicstd   -r -d $DICT)
  c=$(minof $n $T/$f $BD/diclz4   -r -d $DICT)
  printf '%-8s %10s %9s %9s %9s %6s\n' "$f" "$bytes" "$a" "$b" "$c" "$n"
done

echo
echo "=== compressed output sizes (raw, dict, default levels) ==="
for f in in24k in48k in400k in1m6 in3m; do
  za=$($BD/dicflate -r -d $DICT < $T/$f 2>/dev/null | wc -c)
  zb=$($BD/dicstd -r -d $DICT < $T/$f 2>/dev/null | wc -c)
  zc=$($BD/diclz4   -r -d $DICT < $T/$f 2>/dev/null | wc -c)
  printf '%-8s dicflate: %8s  dicstd: %8s  diclz4: %8s\n' "$f" "$za" "$zb" "$zc"
done

# iso-size <dicflate-level>: scan dicstd/diclz4 levels for the output
# size closest to dicflate -r -l <level> with dict, and time those levels
iso_size() {
  local dflvl=$1
  echo
  echo "=== iso-size: dicstd/diclz4 levels with output closest to"
  echo "    dicflate -l ${dflvl}, and the speed at those levels (min of N, ms) ==="
  printf '%-8s %8s %8s %4s %8s %8s %4s %8s %8s %4s\n' \
    "input" "flate" "bytes" "lvl" "dicstd" "bytes" "lvl" "diclz4" "bytes" "lvl"
  for f in in24k in48k in400k in1m6 in3m; do
    case $f in
      in24k)  n=200;; in48k) n=150;; in400k) n=50;; in1m6) n=20;; in3m) n=8;;
    esac
    # dicflate at the given level: the size target and its timing
    target=$($BD/dicflate -r -l $dflvl -d $DICT < $T/$f 2>/dev/null | wc -c)
    ftime=$(minof $n $T/$f $BD/dicflate -r -l $dflvl -d $DICT)

    # scan dicstd levels for the closest output size
    bl=""; bd=999999999; bs=0
    for lvl in $(seq 1 19); do
      s=$($BD/dicstd -r -l $lvl -d $DICT < $T/$f 2>/dev/null | wc -c)
      d=$(( s > target ? s - target : target - s ))
      if (( d < bd )); then bd=$d; bl=$lvl; bs=$s; fi
    done
    stime=$(minof $n $T/$f $BD/dicstd -r -l $bl -d $DICT)

    # scan diclz4 levels for the closest output size
    cl=""; cd=999999999; cs=0
    for lvl in $(seq 0 12); do
      s=$($BD/diclz4 -r -l $lvl -d $DICT < $T/$f 2>/dev/null | wc -c)
      d=$(( s > target ? s - target : target - s ))
      if (( d < cd )); then cd=$d; cl=$lvl; cs=$s; fi
    done
    ltime=$(minof $n $T/$f $BD/diclz4 -r -l $cl -d $DICT)

    printf '%-8s %8s %8s %4s %8s %8s %4s %8s %8s %4s\n' \
      "$f" "$ftime" "$target" "$dflvl" "$stime" "$bs" "$bl" "$ltime" "$cs" "$cl"
  done
}

iso_size 6   # dicflate default
iso_size 1   # dicflate fastest

# minimum compression levels of each tool (not uncompressed storage):
# zlib 1, zstd 1, lz4 0 (lz4 level 0 is fast compression, not stored)
min_level() {
  echo
  echo "=== minimum compression levels: dicflate -l 1, dicstd -l 1, diclz4 -l 0"
  echo "    (min of N, ms) ==="
  printf '%-8s %8s %8s %4s %8s %8s %4s %8s %8s %4s\n' \
    "input" "flate" "bytes" "lvl" "dicstd" "bytes" "lvl" "diclz4" "bytes" "lvl"
  for f in in24k in48k in400k in1m6 in3m; do
    case $f in
      in24k)  n=200;; in48k) n=150;; in400k) n=50;; in1m6) n=20;; in3m) n=8;;
    esac
    ftime=$(minof $n $T/$f $BD/dicflate -r -l 1 -d $DICT)
    fsize=$($BD/dicflate -r -l 1 -d $DICT < $T/$f 2>/dev/null | wc -c)
    stime=$(minof $n $T/$f $BD/dicstd -r -l 1 -d $DICT)
    ssize=$($BD/dicstd -r -l 1 -d $DICT < $T/$f 2>/dev/null | wc -c)
    ltime=$(minof $n $T/$f $BD/diclz4 -r -l 0 -d $DICT)
    lsize=$($BD/diclz4 -r -l 0 -d $DICT < $T/$f 2>/dev/null | wc -c)
    printf '%-8s %8s %8s %4s %8s %8s %4s %8s %8s %4s\n' \
      "$f" "$ftime" "$fsize" "1" "$stime" "$ssize" "1" "$ltime" "$lsize" "0"
  done
}

min_level

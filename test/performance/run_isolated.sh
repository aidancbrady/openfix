#!/usr/bin/env bash
#
# Isolated network benchmark: pins to P-cores, interleaves runs,
# reports median throughput for both engines.
#
# Usage:  ./test/performance/run_isolated.sh [iterations]
#
# For best results, run with sudo to enable frequency pinning + priority.
#
set -euo pipefail

ITERS=${1:-5}

# P-core CPUs on 13900KF (cores 0-3, HT threads 0-7)
BENCH_CPUS="0-7"

# ── helpers ──────────────────────────────────────────────────────────
IS_ROOT=false
[[ $EUID -eq 0 ]] && IS_ROOT=true

cleanup() {
    if $IS_ROOT; then
        for cpu in /sys/devices/system/cpu/cpu[0-9]*/cpufreq/scaling_governor; do
            echo powersave > "$cpu" 2>/dev/null || true
        done
        echo "  Governor restored to powersave"
    fi
}

median() {
    sort -n | awk '{a[NR]=$1} END {
        if (NR%2==1) print a[(NR+1)/2]
        else         print (a[NR/2]+a[NR/2+1])/2
    }'
}

extract_throughput() {
    grep "Network/Throughput" | awk -F'|' '{gsub(/[^0-9]/, "", $3); print $3}'
}

extract_roundtrip() {
    grep "RoundTrip/TestReq-HB" | awk -F'|' '{gsub(/[[:space:]]/, "", $5); print $5}'
}

extract_multileg() {
    grep "RoundTrip/Multileg-AB-8" | awk -F'|' '{gsub(/[[:space:]]/, "", $5); print $5}'
}

run_openfix() {
    taskset -c "$BENCH_CPUS" bazelisk run -c opt //test/performance:openfix-perf 2>&1
}

run_quickfix() {
    taskset -c "$BENCH_CPUS" bazelisk run -c opt //test/performance/quickfix:quickfix-perf 2>&1
}

# ── build ────────────────────────────────────────────────────────────
echo "=== Building benchmarks (-c opt) ==="
bazelisk build -c opt \
    //test/performance:openfix-perf \
    //test/performance/quickfix:quickfix-perf 2>&1 | tail -3

# ── system tuning (if root) ──────────────────────────────────────────
trap cleanup EXIT

if $IS_ROOT; then
    echo ""
    echo "=== Pinning CPU frequency (performance governor) ==="
    for cpu in /sys/devices/system/cpu/cpu[0-9]*/cpufreq/scaling_governor; do
        echo performance > "$cpu"
    done
    FREQ=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq)
    echo "  CPU0 frequency: $((FREQ / 1000)) MHz"
    sync && echo 3 > /proc/sys/vm/drop_caches
    echo "  Filesystem caches dropped"
else
    echo ""
    echo "  (No root — skipping frequency pinning. For best results: sudo $0 $ITERS)"
fi

sleep 1

# ── warmup ───────────────────────────────────────────────────────────
echo ""
echo "=== Warmup run (discarded) ==="
run_openfix > /dev/null
run_quickfix > /dev/null
echo "  Done"
sleep 2

# ── measured runs ────────────────────────────────────────────────────
echo ""
echo "=== Running $ITERS iterations (pinned to P-cores $BENCH_CPUS) ==="
echo ""

OF_THROUGHPUT=()
OF_RT=()
OF_ML=()
QF_THROUGHPUT=()
QF_RT=()
QF_ML=()

for i in $(seq 1 "$ITERS"); do
    echo "--- Iteration $i/$ITERS ---"

    OUTPUT=$(run_openfix)
    T=$(echo "$OUTPUT" | extract_throughput)
    R=$(echo "$OUTPUT" | extract_roundtrip)
    M=$(echo "$OUTPUT" | extract_multileg)
    OF_THROUGHPUT+=("$T")
    OF_RT+=("$R")
    OF_ML+=("$M")
    echo "  openfix:  throughput=${T} msg/s  rt_avg=${R}µs  ml_avg=${M}µs"

    sleep 2

    OUTPUT=$(run_quickfix)
    T=$(echo "$OUTPUT" | extract_throughput)
    R=$(echo "$OUTPUT" | extract_roundtrip)
    M=$(echo "$OUTPUT" | extract_multileg)
    QF_THROUGHPUT+=("$T")
    QF_RT+=("$R")
    QF_ML+=("$M")
    echo "  quickfix: throughput=${T} msg/s  rt_avg=${R}µs  ml_avg=${M}µs"

    sleep 2
done

# ── results ──────────────────────────────────────────────────────────
echo ""
echo "=============================================="
echo "  ISOLATED BENCHMARK RESULTS ($ITERS iterations)"
echo "=============================================="
echo ""

OF_T_MED=$(printf '%s\n' "${OF_THROUGHPUT[@]}" | median)
QF_T_MED=$(printf '%s\n' "${QF_THROUGHPUT[@]}" | median)
OF_R_MED=$(printf '%s\n' "${OF_RT[@]}" | median)
QF_R_MED=$(printf '%s\n' "${QF_RT[@]}" | median)
OF_M_MED=$(printf '%s\n' "${OF_ML[@]}" | median)
QF_M_MED=$(printf '%s\n' "${QF_ML[@]}" | median)

printf "  %-30s %15s %15s\n" "Metric" "openfix" "QuickFIX"
printf "  %-30s %15s %15s\n" "------------------------------" "---------------" "---------------"
printf "  %-30s %12s msg/s %9s msg/s\n" "Network/Throughput (median)" "$OF_T_MED" "$QF_T_MED"
printf "  %-30s %14sµs %11sµs\n" "RoundTrip/TestReq-HB (median)" "$OF_R_MED" "$QF_R_MED"
printf "  %-30s %14sµs %11sµs\n" "RoundTrip/Multileg (median)" "$OF_M_MED" "$QF_M_MED"
echo ""

echo "  All throughput samples (msg/s):"
printf "    openfix:  %s\n" "${OF_THROUGHPUT[*]}"
printf "    quickfix: %s\n" "${QF_THROUGHPUT[*]}"
echo ""
echo "  All round-trip avg samples (µs):"
printf "    openfix:  %s\n" "${OF_RT[*]}"
printf "    quickfix: %s\n" "${QF_RT[*]}"
echo ""
echo "  All multileg avg samples (µs):"
printf "    openfix:  %s\n" "${OF_ML[*]}"
printf "    quickfix: %s\n" "${QF_ML[*]}"

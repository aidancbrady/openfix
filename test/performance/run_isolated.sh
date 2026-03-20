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

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd -- "$SCRIPT_DIR/../.." && pwd)
cd "$REPO_ROOT"

BAZELISK_BIN=$(command -v bazelisk || true)
TASKSET_BIN=$(command -v taskset || true)

if [[ -z "$BAZELISK_BIN" ]]; then
    echo "Unable to find bazelisk in PATH"
    exit 1
fi

if [[ -z "$TASKSET_BIN" ]]; then
    echo "Unable to find taskset in PATH"
    exit 1
fi

# ── helpers ──────────────────────────────────────────────────────────
IS_ROOT=false
[[ $EUID -eq 0 ]] && IS_ROOT=true

BENCH_USER=${SUDO_USER:-$(id -un)}

run_as_bench_user() {
    if $IS_ROOT && [[ -n "${SUDO_USER:-}" ]]; then
        sudo -H -u "$BENCH_USER" -- "$@"
    else
        "$@"
    fi
}

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

first_cpu_in_list() {
    local list=$1
    local first=${list%%,*}
    echo "${first%%-*}"
}

collect_primary_cpus() {
    local require_smt=$1
    declare -A seen=()
    local cpus=()

    for topo in /sys/devices/system/cpu/cpu[0-9]*/topology/thread_siblings_list; do
        [[ -f "$topo" ]] || continue
        local cpu_dir=${topo%/topology/thread_siblings_list}
        if [[ -f "$cpu_dir/online" ]] && [[ $(<"$cpu_dir/online") == "0" ]]; then
            continue
        fi

        local siblings
        siblings=$(<"$topo")
        if [[ "$require_smt" == "1" && "$siblings" != *","* && "$siblings" != *"-"* ]]; then
            continue
        fi

        local primary
        primary=$(first_cpu_in_list "$siblings")
        [[ -n "$primary" ]] || continue
        if [[ -n "${seen[$primary]:-}" ]]; then
            continue
        fi

        seen["$primary"]=1
        cpus+=("$primary")
    done

    if ((${#cpus[@]} == 0)); then
        return 1
    fi

    printf '%s\n' "${cpus[@]}" | sort -n | paste -sd, -
}

detect_bench_cpus() {
    local cpus
    if cpus=$(collect_primary_cpus 1) && [[ -n "$cpus" ]]; then
        echo "$cpus"
        return
    fi

    if cpus=$(collect_primary_cpus 0) && [[ -n "$cpus" ]]; then
        echo "$cpus"
        return
    fi

    echo "0-7"
}

BENCH_CPUS=$(detect_bench_cpus)

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
    run_as_bench_user "$TASKSET_BIN" -c "$BENCH_CPUS" "$BAZELISK_BIN" run -c opt //test/performance:openfix-perf 2>&1
}

run_quickfix() {
    run_as_bench_user "$TASKSET_BIN" -c "$BENCH_CPUS" "$BAZELISK_BIN" run -c opt //test/performance/quickfix:quickfix-perf 2>&1
}

run_and_record() {
    local engine=$1
    local output t r m

    if [[ "$engine" == "openfix" ]]; then
        output=$(run_openfix)
        t=$(echo "$output" | extract_throughput)
        r=$(echo "$output" | extract_roundtrip)
        m=$(echo "$output" | extract_multileg)
        OF_THROUGHPUT+=("$t")
        OF_RT+=("$r")
        OF_ML+=("$m")
        echo "  openfix:  throughput=${t} msg/s  rt_avg=${r}µs  ml_avg=${m}µs"
        return
    fi

    output=$(run_quickfix)
    t=$(echo "$output" | extract_throughput)
    r=$(echo "$output" | extract_roundtrip)
    m=$(echo "$output" | extract_multileg)
    QF_THROUGHPUT+=("$t")
    QF_RT+=("$r")
    QF_ML+=("$m")
    echo "  quickfix: throughput=${t} msg/s  rt_avg=${r}µs  ml_avg=${m}µs"
}

# ── build ────────────────────────────────────────────────────────────
echo "=== Building benchmarks (-c opt) ==="
if $IS_ROOT && [[ -n "${SUDO_USER:-}" ]]; then
    echo "  Running Bazel and benchmark binaries as $BENCH_USER; root is only used for system tuning"
fi

if ! BUILD_OUTPUT=$(run_as_bench_user "$BAZELISK_BIN" build -c opt \
    //test/performance:openfix-perf \
    //test/performance/quickfix:quickfix-perf 2>&1); then
    echo "$BUILD_OUTPUT"
    exit 1
fi
echo "$BUILD_OUTPUT" | tail -3

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
    if (( i % 2 == 1 )); then
        run_and_record openfix
        sleep 2
        run_and_record quickfix
    else
        run_and_record quickfix
        sleep 2
        run_and_record openfix
    fi
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

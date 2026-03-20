#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

if ! command -v perf >/dev/null 2>&1; then
    echo "perf not found in PATH" >&2
    exit 1
fi

BAZELISK_BIN="${BAZELISK_BIN:-}"
if [[ -z "$BAZELISK_BIN" ]]; then
    if command -v bazelisk >/dev/null 2>&1; then
        BAZELISK_BIN="$(command -v bazelisk)"
    elif command -v bazel >/dev/null 2>&1; then
        BAZELISK_BIN="$(command -v bazel)"
    else
        echo "Neither bazelisk nor bazel found in PATH" >&2
        exit 1
    fi
fi

REPEATS="${REPEATS:-3}"
TOP="${TOP:-15}"
LINE_SYMBOLS="${LINE_SYMBOLS:-5}"
LINE_TOP="${LINE_TOP:-3}"
FREQ="${FREQ:-999}"
PERF_STAT_EVENTS="${PERF_STAT_EVENTS:-task-clock,cycles,instructions,branches,branch-misses,cache-misses}"
PERF_DELAY_MS="${PERF_DELAY_MS:-100}"
PROFILE_DEBUG_COPT="${PROFILE_DEBUG_COPT:--g1}"
STRIP_MODE="${STRIP_MODE:-never}"

BUILD_ARGS=(-c opt "--strip=${STRIP_MODE}")
if [[ -n "$PROFILE_DEBUG_COPT" ]]; then
    BUILD_ARGS+=("--copt=${PROFILE_DEBUG_COPT}")
fi
if [[ -n "${BUILD_OPTS:-}" ]]; then
    # shellcheck disable=SC2206
    EXTRA_BUILD_ARGS=(${BUILD_OPTS})
    BUILD_ARGS+=("${EXTRA_BUILD_ARGS[@]}")
fi

declare -A WARMUPS=(
    [heartbeat]="${WARMUP_HEARTBEAT:-100000}"
    [nos]="${WARMUP_NOS:-100000}"
    [multileg]="${WARMUP_MULTILEG:-200000}"
)

declare -A MEASURES=(
    [heartbeat]="${MEASURE_HEARTBEAT:-2000000}"
    [nos]="${MEASURE_NOS:-1500000}"
    [multileg]="${MEASURE_MULTILEG:-3000000}"
)

declare -A LABELS=(
    [heartbeat]="Heartbeat"
    [nos]="NewOrderSingle"
    [multileg]="Multileg"
)

echo "== Building parse-profile =="
"$BAZELISK_BIN" build "${BUILD_ARGS[@]}" //test/performance:parse-profile >/dev/null

BIN="$ROOT/bazel-bin/test/performance/parse-profile"

run_case() {
    local key="$1"
    local label="${LABELS[$key]}"
    local warmup="${WARMUPS[$key]}"
    local measure="${MEASURES[$key]}"
    local perf_data="/tmp/openfix-parse-${key}.perf.data"
    local stat_tmp
    local report_tmp
    local samples_tmp
    local top_syms_tmp
    local wall_tmp
    stat_tmp="$(mktemp)"
    report_tmp="$(mktemp)"
    samples_tmp="$(mktemp)"
    top_syms_tmp="$(mktemp)"
    wall_tmp="$(mktemp)"

    echo
    echo "== ${label} =="
    echo "warmup=${warmup} measure=${measure}"

    /usr/bin/time -f "%e" -o "$wall_tmp" \
        env OPENFIX_PARSE_CASE="$key" OPENFIX_PARSE_WARMUP="$warmup" OPENFIX_PARSE_MEASURE="$measure" \
        "$BIN" >/dev/null 2>/dev/null
    local wall
    wall="$(<"$wall_tmp")"
    awk -v measure="$measure" -v wall="$wall" 'BEGIN {
        printf("wall_time_sec=%.3f\n", wall + 0.0);
        printf("msg_per_sec=%.0f\n", measure / wall);
    }'

    perf stat -r "$REPEATS" -x, -e "$PERF_STAT_EVENTS" \
        env OPENFIX_PARSE_CASE="$key" OPENFIX_PARSE_WARMUP="$warmup" OPENFIX_PARSE_MEASURE="$measure" \
        "$BIN" >/dev/null 2>"$stat_tmp"

    awk -F, '
        $3 == "task-clock"    { task = $1; }
        $3 ~ /cycles/         { cycles += $1; }
        $3 ~ /instructions/   { instructions += $1; }
        $3 ~ /branches/ && $3 !~ /branch-misses/ { branches += $1; }
        $3 ~ /branch-misses/  { branch_misses += $1; }
        $3 ~ /cache-misses/   { cache_misses += $1; }
        END {
            if (task != "")           printf("task_clock_ms=%s\n", task);
            if (cycles > 0)           printf("cycles=%.0f\n", cycles);
            if (instructions > 0)     printf("instructions=%.0f\n", instructions);
            if (branches > 0)         printf("branches=%.0f\n", branches);
            if (branch_misses > 0)    printf("branch_misses=%.0f\n", branch_misses);
            if (cache_misses > 0)     printf("cache_misses=%.0f\n", cache_misses);
            if ((cycles + 0) > 0 && (instructions + 0) > 0)
                printf("ipc=%.3f\n", (instructions + 0) / (cycles + 0));
        }
    ' "$stat_tmp"

    perf record -D "$PERF_DELAY_MS" -F "$FREQ" -g --call-graph fp -o "$perf_data" \
        env OPENFIX_PARSE_CASE="$key" OPENFIX_PARSE_WARMUP="$warmup" OPENFIX_PARSE_MEASURE="$measure" \
        "$BIN" >/dev/null 2>/dev/null

    perf report --stdio -i "$perf_data" --sort symbol >"$report_tmp" 2>&1

    echo "top_hotspots:"
    if grep -q "data has no samples" "$report_tmp"; then
        echo "  no samples captured; increase MEASURE_* or lower PERF_DELAY_MS"
        rm -f "$stat_tmp" "$report_tmp" "$samples_tmp" "$top_syms_tmp" "$wall_tmp"
        return
    fi

    awk -v top="$TOP" '
        BEGIN { shown = 0 }
        /^[[:space:]]*[0-9]+\.[0-9]+%/ {
            line = $0;
            sub(/^[[:space:]]+/, "", line);
            pct = line;
            sub(/[[:space:]].*$/, "", pct);
            sym = line;
            sub(/^([0-9]+\.[0-9]+%[[:space:]]+){1,2}/, "", sym);
            sub(/^\[\.\][[:space:]]+/, "", sym);
            gsub(/[[:space:]]+-[[:space:]]+-[[:space:]]*$/, "", sym);
            gsub(/[[:space:]]+$/, "", sym);
            if (sym == "_start" || sym == "__libc_start_main" || sym == "main" || sym ~ /^0x[0-9a-f]+$/)
                next;
            print pct "\t" sym;
            shown++;
            if (shown >= top)
                exit;
        }
    ' "$report_tmp" >"$top_syms_tmp"

    while IFS=$'\t' read -r pct sym; do
        echo "  $pct  $sym"
    done <"$top_syms_tmp"

    perf script -i "$perf_data" -F ip,sym,srcline >"$samples_tmp" 2>/dev/null || true

    if [[ -s "$samples_tmp" ]]; then
        echo "top_source_lines:"
        awk -v line_symbols="$LINE_SYMBOLS" -v line_top="$LINE_TOP" '
            NR == FNR {
                split($0, parts, "\t");
                if (length(parts) >= 2 && seen_symbols < line_symbols) {
                    top_symbols[++seen_symbols] = parts[2];
                }
                next;
            }
            function flush_frame(    idx) {
                if (current_symbol == "" || current_line == "" || current_line == "??:0")
                    return;
                for (idx = 1; idx <= seen_symbols; ++idx) {
                    if (current_symbol == top_symbols[idx]) {
                        counts[idx, current_line]++;
                        totals[idx]++;
                        break;
                    }
                }
                current_symbol = "";
                current_line = "";
            }
            /^[[:space:]]*[0-9a-f]+[[:space:]]+/ {
                flush_frame();
                current_symbol = $0;
                sub(/^[[:space:]]*[0-9a-f]+[[:space:]]+/, "", current_symbol);
                next;
            }
            /^[[:space:]]*[^[:space:]].*:[0-9]+$/ {
                current_line = $0;
                sub(/^[[:space:]]+/, "", current_line);
                next;
            }
            /^$/ {
                flush_frame();
                next;
            }
            END {
                flush_frame();
                for (idx = 1; idx <= seen_symbols; ++idx) {
                    print "  " top_symbols[idx];
                    if (totals[idx] == 0) {
                        print "    no source lines resolved";
                        continue;
                    }

                    delete best_count;
                    delete best_line;
                    best_size = 0;

                    for (key in counts) {
                        split(key, parts, SUBSEP);
                        if (parts[1] != idx)
                            continue;

                        line = parts[2];
                        count = counts[key];

                        pos = 0;
                        for (slot = 1; slot <= best_size; ++slot) {
                            if (count > best_count[slot]) {
                                pos = slot;
                                break;
                            }
                        }
                        if (pos == 0 && best_size < line_top) {
                            pos = best_size + 1;
                        }
                        if (pos == 0)
                            continue;

                        limit = best_size;
                        if (limit < line_top)
                            limit++;
                        for (slot = limit; slot > pos; --slot) {
                            best_count[slot] = best_count[slot - 1];
                            best_line[slot] = best_line[slot - 1];
                        }
                        best_count[pos] = count;
                        best_line[pos] = line;
                        if (best_size < line_top)
                            best_size++;
                    }

                    for (slot = 1; slot <= best_size; ++slot) {
                        printf("    %.1f%%  %s\n", (100.0 * best_count[slot]) / totals[idx], best_line[slot]);
                    }
                }
            }
        ' "$top_syms_tmp" "$samples_tmp"
    fi

    rm -f "$stat_tmp" "$report_tmp" "$samples_tmp" "$top_syms_tmp" "$wall_tmp"
}

for key in heartbeat nos multileg; do
    run_case "$key"
done

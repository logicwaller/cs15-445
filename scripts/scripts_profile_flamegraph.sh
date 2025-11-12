#!/usr/bin/env bash
set -euo pipefail

# A one-stop perf + FlameGraph profiler for WSL2/Ubuntu (15-445/bustub friendly)
# Features:
# - Launch a command and record perf samples
# - Or attach to an existing process by name or PID
# - Auto-detect perf binary under WSL2 generic linux-tools
# - Auto-install FlameGraph if missing (under ~/FlameGraph)
# - Generate flamegraph.svg under the output directory
#
# Usage examples:
#   scripts/profile_flamegraph.sh --cmd "./build/test/buffer_pool_manager_test --gtest_filter=*" --out ./perf-out --dur 30
#   scripts/profile_flamegraph.sh --proc-name your_binary_name --out ./perf-out --dur 20
#   scripts/profile_flamegraph.sh --pid 12345 --out ./perf-out --dur 15
#
# Recommended build flags (for better stacks):
#   CXXFLAGS="-O2 -g -fno-omit-frame-pointer" CFLAGS="-O2 -g -fno-omit-frame-pointer"
#
# Common WSL2 tweaks (optional if permission errors):
#   sudo sysctl kernel.perf_event_paranoid=-1
#   sudo sysctl kernel.kptr_restrict=0

# Defaults
OUT_DIR="./perf-out"
DUR=20
FREQ=99
CALL_GRAPH="dwarf"   # "fp" also works if compiled with -fno-omit-frame-pointer; "dwarf" is more accurate
PERF_BIN=""
FLAMEGRAPH_DIR="${HOME}/FlameGraph"
CMD=""
PID=""
PROC_NAME=""
EXTRA_PERF_ARGS=()

print_help() {
  cat <<EOF
Usage:
  $0 [--cmd "<command>"] [--pid <pid>] [--proc-name <name>] [--out <dir>] [--dur <seconds>] [--freq <Hz>] [--cg dwarf|fp] [--extra "--switches"]

Options:
  --cmd         Command to run and profile, e.g. "./build/test/buffer_pool_manager_test --gtest_filter=*"
  --pid         Attach to an existing PID
  --proc-name   Attach to the newest process whose name matches (pgrep -n)
  --out         Output directory (default: ${OUT_DIR})
  --dur         Duration in seconds for attachment mode (default: ${DUR})
  --freq        Sampling frequency Hz (default: ${FREQ})
  --cg          Call graph mode: dwarf|fp (default: ${CALL_GRAPH})
  --extra       Extra arguments forwarded to "perf record" (quote the string)
  -h|--help     Show this help

Examples:
  $0 --cmd "./build/test/buffer_pool_manager_test --gtest_filter=*" --out ./perf-out --dur 30
  $0 --proc-name buffer_pool_manager_test --out ./perf-out --dur 20
  $0 --pid 12345 --out ./perf-out --dur 15 --freq 400 --cg fp
EOF
}

# Parse args
while [[ $# -gt 0 ]]; do
  case "$1" in
    --cmd) shift; CMD="${1:-}";;
    --pid) shift; PID="${1:-}";;
    --proc-name) shift; PROC_NAME="${1:-}";;
    --out) shift; OUT_DIR="${1:-}";;
    --dur) shift; DUR="${1:-}";;
    --freq) shift; FREQ="${1:-}";;
    --cg) shift; CALL_GRAPH="${1:-}";;
    --extra) shift; IFS=' ' read -r -a EXTRA_PERF_ARGS <<< "${1:-}";;
    -h|--help) print_help; exit 0;;
    *) echo "Unknown argument: $1" >&2; print_help; exit 1;;
  esac
  shift || true
done

mkdir -p "${OUT_DIR}"

# Locate perf
find_perf() {
  if command -v perf >/dev/null 2>&1; then
    PERF_BIN="$(command -v perf)"
    return
  fi

  # Try the symlink you may have created
  if [[ -x "/usr/local/bin/perf" ]]; then
    PERF_BIN="/usr/local/bin/perf"
    return
  fi

  # Auto-detect under linux-tools generic
  local cand
  cand=$(find /usr/lib/linux-tools/ -maxdepth 2 -type f -name perf 2>/dev/null | sort | tail -n1 || true)
  if [[ -n "${cand}" && -x "${cand}" ]]; then
    PERF_BIN="${cand}"
    return
  fi

  echo "ERROR: perf not found. Install via: sudo apt-get update && sudo apt-get install linux-tools-common linux-tools-generic" >&2
  exit 1
}

# Ensure FlameGraph scripts
ensure_flamegraph() {
  if [[ ! -x "${FLAMEGRAPH_DIR}/flamegraph.pl" ]] || [[ ! -x "${FLAMEGRAPH_DIR}/stackcollapse-perf.pl" ]]; then
    echo "FlameGraph not found at ${FLAMEGRAPH_DIR}. Cloning..."
    git clone https://github.com/brendangregg/FlameGraph.git "${FLAMEGRAPH_DIR}"
  fi
}

# Optional WSL2-friendly sysctl tweaks (best-effort)
maybe_tune_sysctl() {
  if [[ "$(id -u)" -ne 0 ]]; then
    echo "Trying to set WSL2-friendly sysctls (sudo may prompt for password)..."
    sudo sh -c 'sysctl -w kernel.perf_event_paranoid=-1 >/dev/null 2>&1 || true'
    sudo sh -c 'sysctl -w kernel.kptr_restrict=0 >/dev/null 2>&1 || true'
  else
    sysctl -w kernel.perf_event_paranoid=-1 >/dev/null 2>&1 || true
    sysctl -w kernel.kptr_restrict=0 >/dev/null 2>&1 || true
  fi
}

find_perf
ensure_flamegraph
maybe_tune_sysctl

echo "Using perf: ${PERF_BIN}"
echo "Output dir: ${OUT_DIR}"
echo "Call graph: ${CALL_GRAPH}, Freq: ${FREQ} Hz"

OUT_DATA="${OUT_DIR}/perf.data"
OUT_SCRIPT="${OUT_DIR}/out.perf"
OUT_FOLDED="${OUT_DIR}/out.folded"
OUT_SVG="${OUT_DIR}/flamegraph.svg"

record_cmd() {
  echo "Recording with command: ${CMD}"
  # shellcheck disable=SC2086
  sudo "${PERF_BIN}" record -F "${FREQ}" --call-graph "${CALL_GRAPH}" -g -o "${OUT_DATA}" "${EXTRA_PERF_ARGS[@]}" -- bash -lc "${CMD}"
}

record_pid() {
  local target_pid="$1"
  echo "Attaching to PID=${target_pid} for ${DUR}s"
  # shellcheck disable=SC2086
  sudo "${PERF_BIN}" record -F "${FREQ}" --call-graph "${CALL_GRAPH}" -g -o "${OUT_DATA}" -p "${target_pid}" -- sleep "${DUR}"
}

if [[ -n "${CMD}" ]]; then
  record_cmd
elif [[ -n "${PID}" ]]; then
  record_pid "${PID}"
elif [[ -n "${PROC_NAME}" ]]; then
  # newest matching process
  PID="$(pgrep -n "${PROC_NAME}" || true)"
  if [[ -z "${PID}" ]]; then
    echo "ERROR: No running process matched name '${PROC_NAME}'" >&2
    exit 1
  fi
  record_pid "${PID}"
else
  echo "ERROR: You must specify --cmd or --pid or --proc-name" >&2
  print_help
  exit 1
fi

echo "Converting perf.data to script..."
sudo "${PERF_BIN}" script -i "${OUT_DATA}" > "${OUT_SCRIPT}"

echo "Collapsing stacks..."
"${FLAMEGRAPH_DIR}/stackcollapse-perf.pl" "${OUT_SCRIPT}" > "${OUT_FOLDED}"

echo "Generating flame graph SVG..."
"${FLAMEGRAPH_DIR}/flamegraph.pl" "${OUT_FOLDED}" > "${OUT_SVG}"

echo "Done. Open ${OUT_SVG} in your browser."
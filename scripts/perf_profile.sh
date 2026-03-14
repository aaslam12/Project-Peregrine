#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  perf_profile.sh --target /path/to/binary [--artifacts-dir dir] [--label name]
                  [--events event1,event2,...] [--flamegraphs|--no-flamegraphs]
                  [--flamegraph-dir dir] [-- arg1 arg2 ...]

This script records:
  - perf stat output
  - perf record data
  - perf script output
  - flamegraph svg when enabled and FlameGraph tools are available
EOF
}

target=""
artifacts_dir=""
label=""
events="cycles,instructions,branches,branch-misses,L1-icache-load-misses,LLC-load-misses,cache-misses"
flamegraphs="auto"
flamegraph_dir=""
target_args=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --target)
      target="$2"
      shift 2
      ;;
    --artifacts-dir)
      artifacts_dir="$2"
      shift 2
      ;;
    --label)
      label="$2"
      shift 2
      ;;
    --events)
      events="$2"
      shift 2
      ;;
    --flamegraphs)
      flamegraphs="on"
      shift
      ;;
    --no-flamegraphs)
      flamegraphs="off"
      shift
      ;;
    --flamegraph-dir)
      flamegraph_dir="$2"
      shift 2
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    --)
      shift
      target_args=("$@")
      break
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage
      exit 1
      ;;
  esac
done

if [[ -z "${target}" ]]; then
  echo "--target is required" >&2
  exit 1
fi

if [[ ! -x "${target}" ]]; then
  echo "Target is not executable: ${target}" >&2
  exit 1
fi

if [[ -z "${artifacts_dir}" ]]; then
  artifacts_dir="$(pwd)/artifacts/perf"
fi

if [[ -z "${label}" ]]; then
  label="$(basename "${target}")"
fi

timestamp="$(date +%Y%m%d_%H%M%S)"
run_dir="${artifacts_dir}/${timestamp}_${label}"
mkdir -p "${run_dir}"

perf_stat_out="${run_dir}/perf_stat.txt"
perf_data_out="${run_dir}/perf.data"
perf_script_out="${run_dir}/perf.script"
flamegraph_out="${run_dir}/flamegraph.svg"

echo "Artifacts: ${run_dir}"

perf stat -d -d -d -e "${events}" -o "${perf_stat_out}" -- "${target}" "${target_args[@]}"
perf record -g -F 999 -e "${events}" -o "${perf_data_out}" -- "${target}" "${target_args[@]}"
perf script -i "${perf_data_out}" > "${perf_script_out}"

stackcollapse_path=""
flamegraph_path=""
inferno_collapse_path=""
inferno_flamegraph_path=""

if [[ "${flamegraphs}" == "off" ]]; then
  echo "Flamegraph generation disabled."
  exit 0
fi

if [[ -n "${flamegraph_dir}" ]]; then
  if [[ -x "${flamegraph_dir}/stackcollapse-perf.pl" ]]; then
    stackcollapse_path="${flamegraph_dir}/stackcollapse-perf.pl"
  fi
  if [[ -x "${flamegraph_dir}/flamegraph.pl" ]]; then
    flamegraph_path="${flamegraph_dir}/flamegraph.pl"
  fi
fi

if [[ -z "${stackcollapse_path}" ]]; then
  stackcollapse_path="$(command -v stackcollapse-perf.pl || true)"
fi
if [[ -z "${flamegraph_path}" ]]; then
  flamegraph_path="$(command -v flamegraph.pl || true)"
fi
inferno_collapse_path="$(command -v inferno-collapse-perf || true)"
inferno_flamegraph_path="$(command -v inferno-flamegraph || true)"

if [[ -n "${stackcollapse_path}" && -n "${flamegraph_path}" ]]; then
  "${stackcollapse_path}" "${perf_script_out}" | "${flamegraph_path}" > "${flamegraph_out}"
  echo "Flamegraph: ${flamegraph_out}"
elif [[ -n "${inferno_collapse_path}" && -n "${inferno_flamegraph_path}" ]]; then
  "${inferno_collapse_path}" "${perf_script_out}" | "${inferno_flamegraph_path}" > "${flamegraph_out}"
  echo "Flamegraph: ${flamegraph_out}"
elif [[ "${flamegraphs}" == "on" ]]; then
  echo "Flamegraph generation was requested, but no FlameGraph or inferno tools were found." >&2
  exit 1
else
  echo "Flamegraph tools not found. Install Brendan Gregg's FlameGraph tools or Arch's inferno package." >&2
fi

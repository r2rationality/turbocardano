#!/usr/bin/env bash

set -euo pipefail

usage()
{
    cat <<'EOF'
Usage:
  bash test/gen-coverage.sh [TEST_NAME [OUT_DIR [BUILD_DIR]]]
  bash test/gen-coverage.sh --cli [OUT_DIR [BUILD_DIR]] -- TADA_ARGUMENTS...

Examples:
  bash test/gen-coverage.sh 'sync::p2p'
  bash test/gen-coverage.sh --cli -- sync --max-slot=100000 tmp/sync-cov
  bash test/gen-coverage.sh --cli tmp/coverage-sync build-cov -- sync --max-slot=100000 tmp/sync-cov

The CLI workload must exit normally for LLVM to write its raw profile. Use a
bounded command such as sync with --max-slot or --max-epoch.
EOF
}

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
SOURCE_DIR=$(cd -- "$SCRIPT_DIR/.." && pwd)
cd -- "$SOURCE_DIR"

MODE=test
TEST_BIN=tada-test
OUT_DIR=tmp/coverage
BUILD_DIR=build-cov
LLVM_PROFDATA=llvm-profdata-21
LLVM_COV=llvm-cov-21
RUN_ARGS=()

if [[ ${1:-} == --help || ${1:-} == -h ]]; then
    usage
    exit 0
elif [[ ${1:-} == --cli ]]; then
    MODE=cli
    TEST_BIN=tada
    shift
    if [[ ${1:-} != -- && -n ${1:-} ]]; then
        OUT_DIR=$1
        shift
    fi
    if [[ ${1:-} != -- && -n ${1:-} ]]; then
        BUILD_DIR=$1
        shift
    fi
    if [[ ${1:-} == -- ]]; then
        shift
    fi
    if (( $# == 0 )); then
        usage >&2
        exit 2
    fi
    RUN_ARGS=("$@")
else
    RUN_ARGS=("${1:-*}")
    OUT_DIR=${2:-$OUT_DIR}
    BUILD_DIR=${3:-$BUILD_DIR}
    if (( $# > 3 )); then
        usage >&2
        exit 2
    fi
fi

BIN_PATH="$BUILD_DIR/$TEST_BIN"
if [[ ! -x $BIN_PATH ]]; then
    echo "error: coverage executable is missing or not executable: $BIN_PATH" >&2
    echo "configure and build the Coverage target first" >&2
    exit 2
fi

mkdir -p -- "$OUT_DIR"
REPORT_LOCK="$OUT_DIR/.gen-coverage.lock"
if ! mkdir -- "$REPORT_LOCK" 2>/dev/null; then
    echo "error: coverage report directory is already in use: $OUT_DIR" >&2
    echo "use a different OUT_DIR, or remove $REPORT_LOCK after confirming no coverage run is active" >&2
    exit 2
fi
release_report_lock()
{
    rmdir -- "$REPORT_LOCK" 2>/dev/null || true
}
trap release_report_lock EXIT

PROFILE_ROOT=${COVERAGE_PROFILE_DIR:-tmp/coverage-profiles}
mkdir -p -- "$PROFILE_ROOT"
PROFILE_DIR=$(mktemp -d "$PROFILE_ROOT/$TEST_BIN.XXXXXX")
PROFILE_DATA="$PROFILE_DIR/$TEST_BIN.profdata"
export LLVM_PROFILE_FILE="$PROFILE_DIR/%p-%m.profraw"

echo "Running $MODE coverage workload: $BIN_PATH ${RUN_ARGS[*]}"
"$BIN_PATH" "${RUN_ARGS[@]}"

shopt -s nullglob
RAW_PROFILES=("$PROFILE_DIR"/*.profraw)
if (( ${#RAW_PROFILES[@]} == 0 )); then
    echo "error: the workload produced no raw LLVM profiles in $PROFILE_DIR" >&2
    exit 1
fi

echo "Indexing ${#RAW_PROFILES[@]} raw profile(s) into $PROFILE_DATA"
"$LLVM_PROFDATA" merge -sparse "${RAW_PROFILES[@]}" -o "$PROFILE_DATA"

COV_ARGS=(
    show
    -show-branches=percent
    -ignore-filename-regex=3rdparty/
)
if [[ $MODE == test ]]; then
    COV_ARGS+=(-ignore-filename-regex=lib/turbo/cli)
fi
COV_ARGS+=(
    -format=html
    "-output-dir=$OUT_DIR"
    "$BIN_PATH"
    "-instr-profile=$PROFILE_DATA"
)

echo "Generating coverage report into $OUT_DIR"
"$LLVM_COV" "${COV_ARGS[@]}"
echo "Coverage report: $OUT_DIR/index.html"
echo "Raw and indexed profiles: $PROFILE_DIR"

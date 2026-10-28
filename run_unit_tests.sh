#!/usr/bin/env bash
# Builds the unit tests in the raceboat-plugin-builder image and runs them in the raceboat-runtime
# image (the builder's libraceSdkCommon.so needs a newer glibc than the builder ships).
#
#   ./run_unit_tests.sh [--image-tag TAG] [gtest args, e.g. --gtest_filter=TestLink.*]
# TAG defaults to $RACEBOAT_IMAGE_TAG, else 'main'.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TAG="${RACEBOAT_IMAGE_TAG:-main}"

# Leading --image-tag/-t TAG; everything after is passed to the gtest binary.
while [[ $# -gt 0 ]]; do
    case "$1" in
        -t|--image-tag) [[ $# -ge 2 ]] || { echo "$1 requires a value" >&2; exit 1; }; TAG="$2"; shift 2 ;;
        --image-tag=*) TAG="${1#*=}"; shift ;;
        *) break ;;
    esac
done
BUILDER="ghcr.io/tst-race/raceboat/raceboat-plugin-builder:${TAG}"
RUNTIME="ghcr.io/tst-race/raceboat/raceboat-runtime:${TAG}"

docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp \
    -v "${SCRIPT_DIR}":/code -w /code "${BUILDER}" ./build_artifacts.sh --unit-tests

arch_dir="$(ls "${SCRIPT_DIR}/build" | grep '^LINUX' | head -n 1)"
docker run --rm -v "${SCRIPT_DIR}":/code -w /code "${RUNTIME}" \
    "/code/build/${arch_dir}/test/source/unitTestPluginAudius" "$@"

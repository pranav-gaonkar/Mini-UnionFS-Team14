#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FUSE_BINARY="${ROOT_DIR}/mini_unionfs"
TEST_DIR="${ROOT_DIR}/unionfs_test_env"
LOWER_DIR="${TEST_DIR}/lower"
UPPER_DIR="${TEST_DIR}/upper"
MOUNT_DIR="${TEST_DIR}/mnt"

GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

function cleanup() {
    if mountpoint -q "${MOUNT_DIR}"; then
        fusermount3 -u "${MOUNT_DIR}" 2>/dev/null || fusermount -u "${MOUNT_DIR}" 2>/dev/null || umount "${MOUNT_DIR}" 2>/dev/null || true
    fi
    rm -rf "${TEST_DIR}"
}

trap cleanup EXIT

if [[ ! -x "${FUSE_BINARY}" ]]; then
    echo "Missing FUSE binary at ${FUSE_BINARY}. Build the project first (make)."
    exit 1
fi

cleanup
mkdir -p "${LOWER_DIR}" "${UPPER_DIR}" "${MOUNT_DIR}"
echo "base_only_content" > "${LOWER_DIR}/base.txt"
echo "to_be_deleted" > "${LOWER_DIR}/delete_me.txt"

echo "Starting Mini-UnionFS Test Suite..."
"${FUSE_BINARY}" "${LOWER_DIR}" "${UPPER_DIR}" "${MOUNT_DIR}" -f -o auto_unmount &
FUSE_PID=$!
sleep 1

function fail() {
    echo -e "${RED}FAILED${NC}"
}

function pass() {
    echo -e "${GREEN}PASSED${NC}"
}

# Test 1: Layer Visibility
echo -n "Test 1: Layer Visibility... "
if grep -q "base_only_content" "${MOUNT_DIR}/base.txt"; then
    pass
else
    fail
fi

# Test 2: Copy-on-Write
echo -n "Test 2: Copy-on-Write... "
echo "modified_content" >> "${MOUNT_DIR}/base.txt"
if [[ $(grep -c "modified_content" "${MOUNT_DIR}/base.txt") -eq 1 ]] \
   && [[ $(grep -c "modified_content" "${UPPER_DIR}/base.txt") -eq 1 ]] \
   && [[ $(grep -c "modified_content" "${LOWER_DIR}/base.txt") -eq 0 ]]; then
    pass
else
    fail
fi

# Test 3: Whiteout
echo -n "Test 3: Whiteout mechanism... "
rm "${MOUNT_DIR}/delete_me.txt"
if [[ ! -f "${MOUNT_DIR}/delete_me.txt" ]] \
   && [[ -f "${LOWER_DIR}/delete_me.txt" ]] \
   && [[ -f "${UPPER_DIR}/.wh.delete_me.txt" ]]; then
    pass
else
    fail
fi

kill "${FUSE_PID}" 2>/dev/null || true
cleanup
trap - EXIT

echo "Test Suite Completed."

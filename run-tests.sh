#!/usr/bin/env bash
set -euo pipefail

IMAGE="docker.io/zmkfirmware/zmk-dev-x86_64:3.5"
MODULE_DIR="$(cd "$(dirname "$0")" && pwd)"
TESTS_DIR="$MODULE_DIR/tests"
LOG_DIR="$MODULE_DIR/build-logs"
mkdir -p "$LOG_DIR"

docker run --rm \
  -v "$MODULE_DIR":/module:ro \
  -v "$LOG_DIR":/logs \
  -w /workspace \
  "$IMAGE" \
  bash -c '
set -euo pipefail

echo "=== Cloning ZMK v0.3 ==="
git clone --depth 1 --branch v0.3 --quiet https://github.com/zmkfirmware/zmk.git /workspace/zmk
cd /workspace/zmk
west init -l app 2>&1 | tail -1
echo "=== Running west update ==="
west update --fetch-opt=--filter=tree:0 > /dev/null 2>&1
echo "=== West update complete ==="

export ZMK_EXTRA_MODULES=/module

passed=0
failed=0
errors=""

for test_dir in /module/tests/sixdof/*/; do
    test_name="$(basename "$test_dir")"

    if [ ! -f "$test_dir/native_posix_64.keymap" ]; then
        continue
    fi

    echo ""
    echo "=== Building test: $test_name ==="
    if ! west build -p always -b native_posix_64 -d "/workspace/build/$test_name" app -- \
        -DZMK_CONFIG="$test_dir" \
        -DCONFIG_ASSERT=y \
        -DZMK_EXTRA_MODULES=/module 2>&1; then
        echo "FAIL (build error): $test_name"
        failed=$((failed + 1))
        errors="$errors  BUILD: $test_name\n"
        continue
    fi

    echo "--- Running test: $test_name ---"
    exe="/workspace/build/$test_name/zephyr/zmk.exe"
    [ -f "$exe" ] || exe="/workspace/build/$test_name/zephyr/zephyr.exe"
    output=$("$exe" 2>&1 || true)
    echo "$output" > "/logs/test_output_${test_name}.log"
    actual=$(echo "$output" | sed -n "$(cat "$test_dir/events.patterns")")

    if echo "$actual" | diff - "$test_dir/keycode_events.snapshot" > /dev/null 2>&1; then
        echo "PASS: $test_name"
        passed=$((passed + 1))
    else
        echo "FAIL: $test_name"
        echo "  Expected:"
        sed "s/^/    /" "$test_dir/keycode_events.snapshot"
        echo "  Actual:"
        echo "$actual" | sed "s/^/    /"
        echo "  Diff:"
        echo "$actual" | diff - "$test_dir/keycode_events.snapshot" | sed "s/^/    /" || true
        failed=$((failed + 1))
        errors="$errors  SNAPSHOT: $test_name\n"
    fi
done

echo ""
echo "=============================="
echo "Results: $passed passed, $failed failed"
if [ $failed -gt 0 ]; then
    echo -e "Failures:\n$errors"
    exit 1
fi
echo "All tests passed."
' 2>&1 | tee "$LOG_DIR/sixdof-tests.log"

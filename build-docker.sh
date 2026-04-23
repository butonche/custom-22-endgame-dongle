#!/usr/bin/env bash
set -euo pipefail

IMAGE_ARM="docker.io/zmkfirmware/zmk-dev-arm:3.5"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUT_DIR="$SCRIPT_DIR/firmware"
LOG_DIR="$SCRIPT_DIR/build-logs"
mkdir -p "$OUT_DIR" "$LOG_DIR"

# name|board|shield|snippet
BUILDS=(
    "dongle|seeeduino_xiao_ble|xiao_dongle dongle_display|zmk-usb-logging"
    "left|seeeduino_xiao_ble|custom_22_left|"
    "right|seeeduino_xiao_ble|custom_22_right|"
    "trackball|efogtech_trackball_0||zmk-usb-logging"
    "settings_reset|seeeduino_xiao_ble|settings_reset|"
    "settings_reset_trackball|efogtech_trackball_0|settings_reset|"
)

passed=0
failed=0
errors=""

for entry in "${BUILDS[@]}"; do
    IFS='|' read -r name board shield snippet <<< "$entry"

    echo ""
    echo "=== Building: $name ($board / ${shield:-no shield}) ==="

    docker run --rm \
      -v "$SCRIPT_DIR":/repo:ro \
      -v "$OUT_DIR":/firmware \
      "$IMAGE_ARM" \
      bash -c "
mkdir -p /config
cp -r /repo/config/* /config/
cd / && west init -l /config > /dev/null 2>&1
west update --fetch-opt=--filter=tree:0 > /dev/null 2>&1
export ZEPHYR_BASE=/zephyr CMAKE_PREFIX_PATH=\"/zephyr/share/zephyr-package/cmake\"
west build -p always -s /zmk/app -b $board -d /tmp/build \
  ${snippet:+-S \"$snippet\"} -- \
  ${shield:+-DSHIELD=\"$shield\"} \
  -DZMK_CONFIG=\"/config\" \
  -DZMK_EXTRA_MODULES=\"/repo\" 2>&1
if [ -f /tmp/build/zephyr/zmk.uf2 ]; then
  cp /tmp/build/zephyr/zmk.uf2 /firmware/$name.uf2
fi
" > "$LOG_DIR/$name.log" 2>&1

    if [ -f "$OUT_DIR/$name.uf2" ]; then
        echo "PASS: $name ($(ls -lh "$OUT_DIR/$name.uf2" | awk '{print $5}'))"
        passed=$((passed + 1))
    else
        echo "FAIL: $name (see $LOG_DIR/$name.log)"
        failed=$((failed + 1))
        errors="$errors  $name\n"
    fi
done

echo ""
echo "=============================="
echo "Results: $passed passed, $failed failed"
if [ $failed -gt 0 ]; then
    echo -e "Failures:\n$errors"
    exit 1
fi
echo "All builds passed."

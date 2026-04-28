# custom-22-endgame-dongle

ZMK firmware for a split keyboard setup with a trackball, consisting of:

- **Custom 22** — split keyboard (2×11 keys on Seeeduino XIAO BLE)
- **Endgame Trackball** — dual PAW3395 optical sensors (nRF52833) as BLE peripheral
- **Dongle** — XIAO BLE central with SSD1306 OLED display
- **Taipo** input method via rolling combos

## Architecture

```
Keyboard halves (BLE peripherals)
  └─ keys → dongle

Trackball (BLE peripheral)
  └─ sensors → mixer → scroll_scaler → pointer_accel → scroll_accel
     → rate_limiter → XYZ_compress → BLE → dongle

Dongle (BLE central, USB to host)
  └─ BLE → XYZ_decompress → HID mouse / keyboard reports
  └─ layer 4 (6DOF): XYZ_decompress → axis remap → REL_RX/RY/RZ
  └─ 6DOF relay: layer state → BLE GATT → trackball (activates rotation math)
```

All pointer processing runs on the trackball peripheral. The XYZ compressor packs X+Y into a single BLE event to eliminate jitter from split GATT notifications. The dongle only decompresses and sends to the host.

When layer 4 is active, the 6DOF relay (`src/sixdof_relay.c`) writes the active flag to the trackball over a custom GATT characteristic. The mixer switches from pointer output to `ω = (P×V)/|P|²` rotation extraction, emitting `REL_RX/RY/RZ` events that spacenavd reads as a SpaceMouse.

## Hardware

### Trackball
- **MCU:** nRF52833
- **Sensors:** Dual PAW3395 optical (SPI0 + SPI1, CPI 2400)
- **Buttons:** 8 direct GPIO keys
- **Encoders:** 2 × EC11-ISH rotary
- **Battery:** Voltage divider on ADC channel 7

### Keyboard
- **MCU:** Seeeduino XIAO BLE (nRF52840)
- **Keys:** 11 per half, direct GPIO
- **Charging:** 100mA via GPIO hog on P0.13

### Dongle
- **MCU:** Seeeduino XIAO BLE (nRF52840)
- **Display:** SSD1306 128×64 OLED (I2C)
- **Role:** BLE central, USB HID output

## Building

### Docker (local)
```bash
bash build-docker.sh
```
Produces 6 firmware files in `firmware/`:
- `dongle.uf2` — dongle with USB logging
- `left.uf2` — left keyboard half
- `right.uf2` — right keyboard half
- `trackball.uf2` — trackball with USB logging
- `settings_reset.uf2` — NVS reset for XIAO BLE
- `settings_reset_trackball.uf2` — NVS reset for trackball

### GitHub Actions
Push to trigger CI builds via `build.yaml`.

## Configuration

### Pointer and Scroll Sensitivity

In `config/efogtech_trackball_0.conf`:

```ini
# Pointer speed (higher = faster). Default 20.
CONFIG_POINTER_2S_MIXER_DEFAULT_MOVE_COEF=20

# Scroll sensitivity (higher = faster). Default 15.
CONFIG_POINTER_2S_MIXER_DEFAULT_TWIST_COEF=15
```

### Sensor CPI

In `boards/arm/efogtech_trackball_0/pointer.dtsi`, both sensors:
```dts
cpi = <2400>;
```
Both sensors must use the same CPI. Higher CPI produces more raw counts; adjust `MOVE_COEF` to compensate.

### Acceleration Curves

Defined in `pointer.dtsi` as Bezier control points. Each segment is 8 values: `start_x, start_y, end_x, end_y, cp1_x, cp1_y, cp2_x, cp2_y` (scaled by 100).

**Pointer curve** — dampens slow movements:
```dts
curve-data = <0 0 116 41 10 32 16 39
              116 41 4809 100 782 41 171 100>;
```

**Scroll curve** — amplifies after 1/10 scaling:
```dts
curve-data = <0 0 102 100 10 38 10 100
              102 100 5702 406 2271 89 544 355>;
```

### Mixer Tuning

Key settings in `config/efogtech_trackball_0.conf`:

| Setting | Default | Description |
|---------|---------|-------------|
| `SYNC_WINDOW_MS` | 10 | Dual-sensor sync window (ms) |
| `EMA_ALPHA` | 10 | EMA smoothing (1-50, lower=smoother) |
| `SMA_EN` | y | Simple Moving Average smoothing |
| `SCROLL_DISABLES_POINTER` | y | Suppress pointer during twist scroll |
| `STEADY_COOLDOWN` | 200 | Steady-state hold duration (ms) |
| `DELTA_Y_OVER_TRANS_MAG_MUL/DIV` | 9/6 | Y-axis sensitivity ratio |

### Dongle

In `config/xiao_dongle.conf`:
- `CONFIG_ZMK_IDLE_TIMEOUT=30000` — OLED blanks after 30s idle
- `CONFIG_ZMK_SLEEP=n` — always awake (USB powered)
- `CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS=3` — keyboard L+R + trackball

### Keyboard Layers

| Layer | Purpose | Trackball behavior |
|-------|---------|-------------------|
| 0 | Default | Pointer movement |
| 1 | Extras | Copy/paste shortcuts |
| 2 | Scroll | Pointer → scroll wheel |
| 3 | Snipe | Slow precision pointer |
| 4 | 6DOF | SpaceMouse — ball rotation → Rx/Ry/Rz |

Layer 4 is activated by toggling MB5 (toggle stays on until pressed again). Scroll, snipe, and 6DOF layer overrides are in `config/xiao_dongle.overlay`.

### 6DOF SpaceMouse Mode

When layer 4 is active, the trackball emits 3D rotation axes instead of pointer movement:

- Ball rotation → `REL_RX / REL_RY / REL_RZ` events on the evdev node
- Math: `ω = (P × V) / |P|²` — angular velocity from cross product of sensor surface position and velocity vector
- Both sensors contribute independently; their results are averaged
- Pointer movement and twist scroll are suppressed while 6DOF is active

**Host setup (Linux):**
```bash
# Find the dongle's HID evdev node
evtest  # look for REL_RX/RY/RZ axes when rotating ball

# Configure spacenavd to use the dongle
# Add to /etc/spnavrc:
#   device-id <VID> <PID>
systemctl restart spacenavd
```

Works with any spacenavd-aware application (FreeCAD, Blender, etc.).

**Tuning:**
```ini
# In config/efogtech_trackball_0.conf
CONFIG_ZMK_6DOF_SCALE=100   # output scale factor (1-1000)
```

**Kconfig symbols:**

| Symbol | Build | Purpose |
|--------|-------|---------|
| `CONFIG_ZMK_6DOF=y` | trackball | Enables rotation math in mixer |
| `CONFIG_ZMK_6DOF_SCALE` | trackball | Output scale factor (default 100) |
| `CONFIG_ZMK_6DOF_LAYER` | trackball | Layer number that activates 6DOF (default 4) |
| `CONFIG_ZMK_6DOF_RELAY=y` | both | BLE GATT relay: central sends active flag to peripheral |
| `CONFIG_ZMK_INPUT_PROCESSOR_6DOF=y` | dongle | Axis remapper (X/Y/Z → RX/RY/RZ) |

## Processing Chain

### Peripheral (trackball)
```
PAW3395 sensors → bridge code → 2-sensor mixer →
  scroll_scaler (1/10, internal remainders) →
  pointer_accel (Bezier curve) →
  scroll_accel (Bezier curve) →
  rate_limiter (8ms, BLE only) →
  XYZ compressor (packs X+Y into single event) →
  input-split → BLE

6DOF mode (layer 4 active, signaled via GATT relay):
  2-sensor mixer → ω=(P×V)/|P|² per sensor → average →
  scale by CONFIG_ZMK_6DOF_SCALE → emit REL_X/Y/Z carrying Rx/Ry/Rz →
  XYZ compressor → BLE
```

### Dongle
```
BLE → input-split proxy → input-listener →
  XYZ decompressor → HID mouse report

Layer overrides:
  Scroll (layer 2): XYZ decompress → scale 1/3 → remap to scroll → invert Y
  Snipe  (layer 3): XYZ decompress → scale 1/4
  6DOF   (layer 4): XYZ decompress → axis remap (X→RX, Y→RY, Z→RZ) → evdev
```


## External Modules

| Module | Source | Purpose |
|--------|--------|---------|
| zmk-paw3395-driver | efogdev | PAW3395 sensor driver |
| zmk-ec11-ish-driver | efogdev | EC11-ISH encoder driver |
| zmk-feature-rolling-combos | butonche | Overlapping combo detection |
| zmk-dongle-display | englmaxi | OLED battery/WPM/layer display |
| zmk-input-processor-xyz | badjeff | XYZ compress/decompress for BLE |

## Bundled Processors

Stripped from efogdev modules — compile-time config only, no NVS/shell/behaviors.

| Source | Purpose |
|--------|---------|
| `src/pointer_2s_mixer.c` | Dual-sensor mixing, twist scroll, SMA smoothing, 6DOF rotation extraction |
| `src/accel_curve.c` | Bezier acceleration curves |
| `src/scroll_scaler.c` | Scroll scaling with internal remainder tracking |
| `src/report_rate_limit.c` | BLE report rate throttling |
| `src/sixdof_mode.c` | 6DOF active flag + `sixdof_is_active()` |
| `src/sixdof_relay.c` | BLE GATT relay: sends layer 4 state from dongle to trackball |
| `src/input_processor_6dof.c` | Dongle-side input processor: remaps X/Y/Z → RX/RY/RZ |

## Tests

```bash
bash run-tests.sh
```

Runs native_posix_64 integration tests inside Docker (`zmk-dev-x86_64:3.5`). Output saved to `build-logs/test_output_<name>.log`.

| Test | What it checks |
|------|---------------|
| `tests/sixdof/rotate_ry/` | Sensor1 dx=100 → `rx=-10 ry=-49 rz=40` (6DOF math with known geometry) |

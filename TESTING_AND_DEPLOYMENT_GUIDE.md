# Testing and Deployment Guide for Adaptive Runtime Tuning
## linuxptp v4.4

This guide covers testing, validation, and deployment of the adaptive runtime tuning
framework, including the adaptive automation layer and per-GM profiles.

---

## Table of Contents

1. [Build Verification](#1-build-verification)
2. [Unit Validation (Offline)](#2-unit-validation-offline)
3. [Runtime Test Suite](#3-runtime-test-suite)
4. [Adaptive Engine Validation](#4-adaptive-engine-validation)
5. [Per-GM Profile Validation](#5-per-gm-profile-validation)
6. [Parameter Boundary Testing](#6-parameter-boundary-testing)
7. [Stress Testing](#7-stress-testing)
8. [Deployment Guide](#8-deployment-guide)
9. [Troubleshooting](#9-troubleshooting)

---

## 1. Build Verification

### Prerequisites
- Linux with kernel headers
- GCC or cross-compiler (e.g., `aarch64-linux-gnu-gcc`)
- `make`, `git`

### Build Commands

```bash
# Clean build
make clean
make

# Cross-compilation example
make CROSS_COMPILE=aarch64-linux-gnu-

# With additional debug flags
make DEBUG=-g EXTRA_CFLAGS="-DVERBOSE_DEBUG"
```

### Expected Output
- All object files compile without warnings
- `adap.o` is linked into `ptp4l` binary
- No `implicit declaration` or `undefined reference` errors

### Quick Build Test
```bash
# Check that adap.o is in the build
make 2>&1 | grep -i adap
# Should show: adap.o compiled and linked
```

---

## 2. Unit Validation (Offline)

### Python Struct Validation
The `validate_runtime_tuning.py` script validates TLV struct definitions,
byte ordering, and parameter ranges. No ptp4l required.

```bash
python3 validate_runtime_tuning.py
```

### Expected Output
```
Adaptive Runtime Tuning - TLV Validation

=== Struct Size Validation ===
  servo_settings_np      8 bytes (expected  8) ... ✓ PASS
  pi_constants_np       24 bytes (expected 24) ... ✓ PASS
  tsproc_filter_np       8 bytes (expected  8) ... ✓ PASS
  clock_freq_est_np      4 bytes (expected  4) ... ✓ PASS

=== Byte Order (Network Endianness) ===
  ServoSettingsNp: pack/unpack round-trip ... ✓ PASS
  PiConstantsNp:   pack/unpack round-trip ... ✓ PASS
  TsprocFilterNp:  pack/unpack round-trip ... ✓ PASS
  ClockFreqEstNp:  pack/unpack round-trip ... ✓ PASS

=== Summary ===
✓ All validation tests PASSED
```

### Code Review Checklist
- [ ] `adap.h` - All function declarations match `adap.c` implementations
- [ ] `adap.c` - No dead code or unused functions
- [ ] `clock.c` - `adap_create()` called in `clock_create()`
- [ ] `clock.c` - `adap_feed_sample()` and `adap_evaluate()` called in `clock_synchronize()`
- [ ] `clock.c` - `adap_on_gm_change()` called in `handle_state_decision_event()`
- [ ] `clock.c` - `adap_destroy()` called in `clock_destroy()`
- [ ] `config.c` - Config options present with proper defaults

---

## 3. Runtime Test Suite

### Prerequisites
- ptp4l running with management interface
- pmc tool available
- Root/sudo access for hardware timestamping

### Setup
```bash
# Terminal 1: Start ptp4l with adaptive tuning enabled (default)
sudo ptp4l -i eth0 -m &

# Or with specific config
sudo ptp4l -i eth0 -f configs/default.cfg -m &

# Verify ptp4l started
sudo pmc -d 0 get TIME_STATUS_NP
```

### Run the Runtime Test Suite
```bash
# Run the full test suite
./test_adap_tuning.sh 0
```

### Test Phases
| Phase | Description | Est. Time |
|-------|-------------|-----------|
| 0 | Environment check (pmc, ptp4l reachability) | 5s |
| 1 | Adaptive engine config validation | 3s |
| 2 | Mode transitions (conservative/balanced/aggressive) | 15s |
| 3 | Per-GM profile simulation | 10s |
| 4 | Parameter boundary & edge case tests | 20s |
| 5 | Stress test (rapid cycling) | 10s |
| 6 | Restoration & cleanup | 5s |

### Expected Results
```
Phase 0: Prerequisites & Environment Check
  pmc found: /usr/sbin/pmc
  ptp4l reachable ✓

Phase 2: Adaptive Engine – Mode Transitions
  [PASS] SET conservative servo params
  [PASS] SET conservative PI constants
  [PASS] SET conservative filter length
  [PASS] SET conservative freq est interval

Phase 4: Parameter Validation & Edge Cases
  [PASS] numOffsetValues=1 (min boundary)
  [PASS] numOffsetValues=0 (below min, should reject)
  [PASS] kp=10.0 (max boundary)
  [PASS] kp=-0.1 (below min, should reject)
  [PASS] filter_length=256 (max)
  [PASS] filter_length=0 (below min, should reject)

Test Summary
  Total tests : 25
  Passed      : 25
  Failed      : 0

✓ ALL TESTS PASSED
```

---

## 4. Adaptive Engine Validation

### 4.1 Verify Engine Creation

Check ptp4l startup log for:
```
ADAP: created adaptive engine, mode=balanced window=10 enabled=1
```

### 4.2 Verify Metric Collection

Enable debug logging and observe:
```bash
# In ptp4l startup, add -l 6 for debug logging
sudo ptp4l -i eth0 -m -l 6

# Expected log lines:
# ADAP: metrics jitter=45.2ns mean=12.3ns delay_var=8.1ns loss=0 stable=5 state=3
```

### 4.3 Verify Auto Mode Switching

Under stable network conditions (jitter < 100ns for 20+ samples):
```
ADAP: switching mode 1 -> 2 (jitter=45.2 loss=0 stable=25)
ADAP: applied params: numOff=4 offThr=50000 kp=1.000 ki=0.500 ...
```

Under noisy conditions (jitter > 500ns):
```
ADAP: switching mode 1 -> 0 (jitter=623.8 loss=0 stable=0)
ADAP: applied params: numOff=8 offThr=200000 kp=0.500 ki=0.200 ...
```

Under packet loss (> 5 missed syncs):
```
ADAP: switching mode 1 -> 0 (jitter=45.2 loss=7 stable=0)
```

### 4.4 Manual Mode Override

If mode is manually set (non-auto mode), the engine log will show:
```
ADAP: mode manually set to conservative
```
And auto-switching will be disabled until ptp4l restart.

---

## 5. Per-GM Profile Validation

### 5.1 Default Profile (No Explicit Profile)

When ptp4l detects a GM change and no per-GM profile is configured:
```
ADAP: GM changed to 00:1B:A1:23:45:67:89:AB, using default balanced mode
ADAP: metrics reset
```

### 5.2 Custom Profile (Deployment File)

Set `adap_gm_profile_file` in the ptp4l config:
```ini
adap_gm_profile_file /etc/linuxptp/adap-gm-profiles.csv
```

Example profile file:
```text
# gmIdentity,label,numOffsetValues,offsetThreshold,kp,ki,interval,filterLength,freqEstInterval,stepThresholdNs,firstStepThresholdNs,maxFrequencyPpb
001ba1.2345.6789ab,DC-east-core,8,200000,0.5,0.2,1.0,16,3,0,20000,900000000
```

Expected behaviour on GM switch to this identity:
```
ADAP: GM changed to 00:1B:A1:23:45:67:89:AB, applying profile 'DC-east-core'
ADAP: applied params: numOff=8 offThr=200000 kp=0.500 ki=0.200 ...
```

### 5.3 Custom Profile (Programmatic API)

Using the C API to add a GM profile:
```c
struct adap *a = clock_get_adap(clock);
struct ClockIdentity gm_id = { .id = {0x00, 0x1B, 0xA1, ...} };
struct adap_params params = {
    .num_offset_values = 8,
    .kp = 0.5, .ki = 0.2,
    .filter_length = 16,
    .freq_est_interval = 3,
};
adap_set_gm_profile(a, gm_id, &params, "DC-east-core");
```

Expected behaviour on GM switch to this identity:
```
ADAP: GM changed to 00:1B:A1:23:45:67:89:AB, applying profile 'DC-east-core'
ADAP: applied params: numOff=8 offThr=200000 kp=0.500 ki=0.200 ...
```

### 5.4 Profile Update and Removal

```c
// Update existing profile
adap_set_gm_profile(a, gm_id, &new_params, "DC-east-v2");

// Remove profile (falls back to default mode params)
adap_remove_gm_profile(a, gm_id);
```

### 5.5 Manual GM Change Simulation

Trigger a BMC re-evaluation to simulate GM change:
```bash
# Change priority to force re-election
sudo pmc -d 0 set PRIORITY1 val 129
sleep 2
sudo pmc -d 0 set PRIORITY1 val 128
```

---

## 6. Parameter Boundary Testing

### 6.1 Servo Settings (MID_SERVO_SETTINGS_NP)

| Parameter       | Min | Max  | Below Min | Above Max | Negative     |
|-----------------|-----|------|-----------|-----------|--------------|
| numOffsetValues | 1   | 100  | 0 ✗       | 101 ✗     | N/A          |
| offsetThreshold | 0   | Max  | N/A       | N/A       | -1 ✗         |

### 6.2 PI Constants (MID_PI_CONSTANTS_NP)

| Parameter | Min | Max  | Below Min | Above Max | Zero    |
|-----------|-----|------|-----------|-----------|---------|
| kp        | 0.0 | 10.0 | -0.1 ✗    | 10.1 ✗    | 0.0 ✓   |
| ki        | 0.0 | 10.0 | -0.1 ✗    | 10.1 ✗    | 0.0 ✓   |
| interval  | >0  | 100  | -1.0 ✗    | 101.0 ✗   | 0.0 ✗   |

### 6.3 Tsproc Filter (MID_TSPROC_FILTER_NP)

| Parameter    | Min | Max | Below Min | Above Max |
|--------------|-----|-----|-----------|-----------|
| filter_length| 1   | 256 | 0 ✗       | 257 ✗     |

### 6.4 Clock Freq Est (MID_CLOCK_FREQ_EST_NP)

| Parameter         | Min | Max | Below Min | Above Max |
|-------------------|-----|-----|-----------|-----------|
| freq_est_interval | -8  | 20  | -9 ✗      | 21 ✗      |

### 6.5 Servo Thresholds (MID_SERVO_THRESHOLDS_NP)

| Parameter           | Min | Max      | Below Min  |
|---------------------|-----|----------|------------|
| step_threshold      | 0.0 ns | 1000000000.0 ns | -0.1 ✗ |
| first_step_threshold| 0.0 ns | 1000000000.0 ns | -0.1 ✗ |
| max_frequency       | 0   | 1000000000 | -1 ✗      |

✗ = Expected rejection with `MID_WRONG_VALUE` error

---

## 7. Stress Testing

### 7.1 Rapid Parameter Cycling

The test suite cycles through 10 parameter sets rapidly:
```bash
./test_adap_tuning.sh 0
```
Expected: ptp4l stays responsive after all cycles.

### 7.2 Long-Running Stability Test

```bash
# Run for 1 hour with parameter changes every 30 seconds
for i in $(seq 1 120); do
    case $((RANDOM % 3)) in
        0) pmc -d 0 "SET SERVO_SETTINGS_NP 8 200000"
           pmc -d 0 "SET PI_CONSTANTS_NP 0.5 0.2 1.0" ;;
        1) pmc -d 0 "SET SERVO_SETTINGS_NP 5 100000"
           pmc -d 0 "SET PI_CONSTANTS_NP 0.7 0.3 1.0" ;;
        2) pmc -d 0 "SET SERVO_SETTINGS_NP 4 50000"
           pmc -d 0 "SET PI_CONSTANTS_NP 1.0 0.5 1.0" ;;
    esac
    sleep 30
done
```

### 7.3 Memory Leak Check

Run with valgrind for 1 hour of stress testing:
```bash
sudo valgrind --leak-check=full --show-leak-kinds=all \
    ptp4l -i eth0 -m &
# After test: check valgrind output for:
# - "definitely lost: 0 bytes"
# - No leaks in adap.c allocation paths
```

---

## 8. Deployment Guide

### 8.1 Configuration Reference

```ini
# /etc/linuxptp/ptp4l.conf (or custom config)

[global]

# Adaptive tuning engine
adap_tuning_enabled       1         # Enable adaptive tuning (default: 1)
adap_tuning_mode          auto      # auto|conservative|balanced|aggressive
                                    # auto switches based on measured conditions.
                                    # Named modes are fixed deployment profiles.
adap_eval_interval        1.0       # Seconds between evaluations (0.1 - 60.0)
adap_sample_window        10        # Rolling window for metrics (2 - 100)
adap_gm_profile_file      /etc/linuxptp/adap-gm-profiles.csv

# Traditional servo settings (still apply as initial/fallback values)
servo_num_offset_values   10
servo_offset_threshold    0
step_threshold            0.0
first_step_threshold      0.00002
max_frequency             900000000
pi_proportional_const     0.0
pi_integral_const         0.0
```

Per-GM profile file format:
```text
# gmIdentity,label,numOffsetValues,offsetThreshold,kp,ki,interval,filterLength,freqEstInterval,stepThresholdNs,firstStepThresholdNs,maxFrequencyPpb
001ba1.2345.6789ab,Nokia-core-A,8,200000,0.5,0.2,1.0,16,3,0,20000,900000000
001ba1.2345.6789ac,Nokia-edge-B,5,100000,0.7,0.3,1.0,10,2,0,20000,900000000
```

### 8.2 Deployment Profiles

#### Datacenter / Stable Network
```ini
adap_tuning_enabled       1
adap_tuning_mode          aggressive
adap_eval_interval        5.0        # Less frequent evaluation
adap_sample_window        20         # Larger window for stability
```

#### Telecom / Mobile Backhaul
```ini
adap_tuning_enabled       1
adap_tuning_mode          balanced
adap_eval_interval        1.0
adap_sample_window        10
```

#### Industrial / Noisy Environment
```ini
adap_tuning_enabled       1
adap_tuning_mode          conservative
adap_eval_interval        2.0
adap_sample_window        15
```

#### Disable Adaptive Tuning (Use Static Config)
```ini
adap_tuning_enabled       0
```

### 8.3 Monitoring

Key log patterns to monitor with log aggregation (e.g., `grep`, `journalctl`):

```bash
# Engine creation
journalctl -u ptp4l | grep "ADAP: created adaptive engine"

# Mode transitions
journalctl -u ptp4l | grep "ADAP: switching mode"

# GM changes
journalctl -u ptp4l | grep "ADAP: GM changed to"

# Parameter application
journalctl -u ptp4l | grep "ADAP: applied params"

# Parameter validation (rejected values)
journalctl -u ptp4l | grep "REJECTED"
```

### 8.4 Gradual Rollout Strategy

1. **Phase 1 - Monitor Only:** Deploy with `adap_tuning_enabled=0`. Check logs for
   engine creation and metric collection without parameter changes.

2. **Phase 2 - Conservative:** Enable with `adap_tuning_mode=conservative`.
   Observe effect on offset jitter and lock stability.

3. **Phase 3 - Auto:** Allow auto-mode by setting `adap_tuning_mode=auto`.
   Monitor mode transitions during network condition changes.

4. **Phase 4 - Full Auto:** Enable aggressive auto-detection. Enable if network
   conditions are proven stable.

---

## 9. Troubleshooting

### Build Errors

| Error | Cause | Fix |
|-------|-------|-----|
| `implicit declaration of LIST_FOREACH_SAFE` | Missing `missing.h` | Add `#include "missing.h"` |
| `undefined reference to adap_*` | adap.o not linked | Add `adap.o` to OBJ in makefile |
| `adap.h: No such file or directory` | File not created | Create `adap.h` in source directory |

### Runtime Errors

| Error | Cause | Fix |
|-------|-------|-----|
| `ADAP: created adaptive engine` not showing | adap_create() failed | Check malloc, config reads |
| `REJECTED: numOffsetValues 0 out of range` | Invalid PMC command | Use values in [1, 100] |
| ptp4l crash on parameter change | NULL pointer in setter | Check servo/tsproc not NULL |
| No mode switching | manual_mode=1 | Only happens if adap_set_mode() was called |

### Validation Checklist

- [ ] Build succeeds with no warnings
- [ ] Python struct validation passes
- [ ] ptp4l starts without adaptive engine errors
- [ ] PMC can GET/SET all runtime tuning TLVs
- [ ] Boundary values are properly rejected
- [ ] Stress test does not crash ptp4l
- [ ] GM change detection logs appear in ptp4l output
- [ ] Adaptive engine metrics log periodically
- [ ] Mode transitions occur under simulated conditions
- [ ] All parameters restored after cleanup

---

## Appendix: Quick Reference

```bash
# ==== Quick Start ====
make clean && make
sudo ptp4l -i eth0 -m &
./test_adap_tuning.sh 0

# ==== Manual Tests ====
# Get current adaptive engine metrics
python3 -c "
from validate_runtime_tuning import *
test_struct_sizes()
test_byte_order()
"

# PMC parameter changes
pmc -d 0 "SET SERVO_SETTINGS_NP 8 150000"
pmc -d 0 "SET PI_CONSTANTS_NP 0.75 0.35 1.2"
pmc -d 0 "SET TSPROC_FILTER_NP 0 10"
pmc -d 0 "SET CLOCK_FREQ_EST_NP 3"

# Verify changes
pmc -d 0 "GET SERVO_SETTINGS_NP"
pmc -d 0 "GET PI_CONSTANTS_NP"

# Restore defaults
pmc -d 0 "SET SERVO_SETTINGS_NP 5 100000"
pmc -d 0 "SET PI_CONSTANTS_NP 0.7 0.3 1.0"
```

---

*Document version: 1.0 - 2026-07-03*
*Authors: Kuldip Dwivedi <kuldip.dwivedi@happiestminds.com>*

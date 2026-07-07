# Adaptive Runtime Tuning for linuxptp v4.4 - Implementation Patch
==================================================================

## COMMIT
runtime: add parameter validation, audit logging, gradual ramp, and build fixes
14abb30 (HEAD -> master)

## FILES MODIFIED
1. print.h           - Added PRINT_LEVEL_MIN/MAX, print() declaration, pl_warning/pl_err macros, pr_tune/ pr_tune_str for audit logging
2. clock.c           - Added parameter validation, audit logging in SET handlers, gradual ramp for PI changes
3. port.c            - Added per-port TSPROC_FILTER_NP validation and audit logging
4. clockadj.c        - Added #include "util.h" for is_sys_clock()
5. monitor.c         - Added #include "util.h" for pid_eq()
6. pmc_common.c      - Added #include "util.h" for str2cid/str2pid/ptp_text_set()
7. sad.h             - Added #include "config.h" for struct config
8. sad.c             - Added #include "util.h" for base64_decode()
9. ts2phc.c          - Added #include "util.h" for pid2str/port_state_normalize/etc
10. tz2alt.c         - Added #include "util.h" for is_running/handle_term_signals/get_arg_val_i

## NEW FEATURES

### 1. Parameter Validation
All runtime tuning TLVs now validate parameter ranges before applying:
- SERVO_SETTINGS_NP: numOffsetValues [1, 100], offsetThreshold >= 0
- PI_CONSTANTS_NP: kp/ki [0.0, 10.0], interval (0.0, 100.0]
- CLOCK_FREQ_EST_NP: freq_est_interval [-8, 20], additionally guarded against unsafe sync-interval shifts
- SERVO_THRESHOLDS_NP: step_threshold/first_step_threshold [0.0 ns, 1000000000.0 ns], max_frequency [0, 1e9]
- TSPROC_FILTER_NP: filter_length [1, 256]

Out-of-range values are rejected with MID_WRONG_VALUE error and logged.

### 2. Tuning History/Audit Logging
Added pr_tune() and pr_tune_str() macros to print.h for consistent audit logging.
All SET handlers log old→new values with format: "TUNE: param old -> new"

Examples:
  TUNE: numOffsetValues 10 -> 5
  TUNE: kp 0.700000 -> 0.500000
  TUNE: port[eth0] filter_length 8 -> 12

### 3. Gradual Ramp for PI Changes
Implemented slew-rate limiting in MID_PI_CONSTANTS_NP handler:
- If |kp - old_kp| > 0.2 or |ki - old_ki| > 0.2, an intermediate step at 50% of the delta is applied first
- Final target values are applied immediately after the ramp
- Logged with: "PI ramp: applying intermediate kp=0.600 ki=0.250 (target kp=1.000 ki=0.500)"

### 4. Per-Port TSPROC_FILTER_NP
Extended port_management_set() MID_TSPROC_FILTER_NP case with:
- Length validation [1, 256] with per-port error log
- Audit logging with port name prefix: TUNE: port[eth0] filter_type 0 -> 1

## BUILD FIXES
- Added PRINT_LEVEL_MIN/MAX macros to print.h
- Added missing pl_warning() and pl_err() macros to print.h
- Fixed print() function declaration in print.h
- Fixed missing util.h includes in: clockadj.c, monitor.c, pmc_common.c, sad.c, ts2phc.c, tz2alt.c
- Fixed missing config.h include in sad.h

All changes maintain backward compatibility with existing code.

## TESTING
Build verified successfully on aarch64-linux-gnu-gcc with -Wall.

## REFERENCES
- IEEE 1588-2008: PTP Management Messages and TLVs
- linuxptp Management Interface: pmc_common.c, tlv.c
- Servo API: servo.h, servo.c, pi.c

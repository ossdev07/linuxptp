Adaptive Runtime Tuning for linuxptp v4.4 - Implementation Patch
==================================================================

SUMMARY
-------
This patch implements runtime adaptive tuning of servo parameters, PI controller constants,
timestamp processor filters, and clock frequency estimation intervals via PTP management
interface (PMC), without requiring restart, configuration reload, SIGHUP, or external scripts.

COMMIT
------
runtime: expose adaptive runtime tuning via PMC TLVs (servo/pi/tsproc/clock)
5e75b7f (HEAD -> master)

FILES MODIFIED
--------------
1. tlv.h           - Added 4 new management TLV IDs and payload structs
2. tlv.c           - Implemented network byte-order conversions for new TLVs
3. clock.c         - Added management SET handlers for servo/PI/clock parameters
4. port.c          - Added management SET handler for tsproc filter parameters
5. pmc_common.c    - Added CLI parsing and TLV datalen support for new parameters
6. pmc.c           - Added display formatting for new TLV GET/SET responses

NEW MANAGEMENT TLV IDs
----------------------
MID_SERVO_SETTINGS_NP      0xC00E   - Servo: numOffsetValues, offsetThreshold
MID_PI_CONSTANTS_NP        0xC00F   - PI Servo: kp, ki, interval
MID_TSPROC_FILTER_NP       0xC010   - Tsproc: filter_type, filter_length
MID_CLOCK_FREQ_EST_NP      0xC011   - Clock: freq_est_interval

RUNTIME TUNING CAPABILITIES
-----------------------------

1. SERVO SETTINGS
   Parameter: numOffsetValues (int32)
     Default: 5
     Allows: Adjusting servo sample count for offset filtering
   Parameter: offsetThreshold (int32, nanoseconds)
     Default: 100000
     Allows: Setting servo frequency step threshold

   PMC Usage:
   $ pmc -d 0 set SERVO_SETTINGS_NP numOffsetValues 8 offsetThreshold 150000

2. PI CONTROLLER CONSTANTS
   Parameter: kp (double)
     Default: 0.7
     Allows: Proportional gain tuning
   Parameter: ki (double)
     Default: 0.3
     Allows: Integral gain tuning
   Parameter: interval (double)
     Default: 1.0
     Allows: Controller interval adjustment

   PMC Usage:
   $ pmc -d 0 set PI_CONSTANTS_NP kp 0.75 ki 0.35 interval 1.2

3. TIMESTAMP PROCESSOR FILTER
   Parameter: filter_type (uint16)
     Default: 0
     Allows: Filter algorithm selection (0=moving_average, 1=median, etc.)
   Parameter: filter_length (int32)
     Default: 8
     Allows: Adjusting filter window size for timestamp smoothing

   PMC Usage:
   $ pmc -d 0 set TSPROC_FILTER_NP filter_type 0 filter_length 10

4. CLOCK FREQUENCY ESTIMATION
   Parameter: freq_est_interval (int32)
     Default: 256
     Allows: Setting frequency estimation window for long-term stability

   PMC Usage:
   $ pmc -d 0 set CLOCK_FREQ_EST_NP freq_est_interval 512

TECHNICAL DETAILS
-----------------

Architecture:
  - Management TLV definitions use big-endian (network byte order)
  - Payload structs in tlv.h define field types and sizes
  - tlv.c handles host↔network conversions (HTONL for int32, NTOHL, net2host64/host2net64 for doubles)
  - Runtime setter APIs (servo_set_*, pi_servo_set_*, tsproc_set_*, clock_set_*) called via
    management SET handlers in clock_management_set() and port_management_set()

Setter Functions Used:
  servo_set_num_offset_values()     - Modifies servo sample count
  servo_set_offset_threshold()      - Modifies servo frequency step threshold
  pi_servo_set_constants()          - Updates PI gains and interval
  tsproc_set_filter_length()        - Reconstructs filter with new length
  clock_set_freq_est_interval()     - Updates frequency estimation window

Management Dispatch:
  When management GET/SET arrives with new MID_* IDs:
  1. tlv.c converts network bytes to host format
  2. pmc_common.c parses CLI arguments into struct
  3. pmc_send_set_action() sends TLV to daemon
  4. clock_management_set() / port_management_set() handles SET
  5. Setter function applies change immediately
  6. Response sent back via pmc client

TESTING
-------

Quick Verification:
  $ python validate_runtime_tuning.py      # Verify struct sizes and endianness
  $ bash test_runtime_tuning.sh 0          # Full PMC integration test

Manual Testing:
  1. Build: make (on Linux with gcc)
  2. Run ptp4l: ptp4l -i eth0 -m -l 5
  3. Query settings: pmc -d 0 get SERVO_SETTINGS_NP
  4. Modify: pmc -d 0 set SERVO_SETTINGS_NP numOffsetValues 10 offsetThreshold 200000
  5. Verify: pmc -d 0 get SERVO_SETTINGS_NP (check new values)
  6. Check logs: ptp4l should show tuning applied without restart

COMPATIBILITY
--------------
- Backward compatible: Only adds new management TLVs, doesn't modify existing ones
- No configuration file changes required
- No restart needed for tuning changes to take effect
- Works with existing PMC clients that don't support new TLVs (ignored)
- Requires PMC client compiled with new ID definitions

LIMITATIONS & FUTURE WORK
--------------------------
- Parameter validation: Callers should validate ranges before PMC SET
- No persistence: Runtime tuning is lost on daemon restart (use config file for permanent settings)
- Per-port/per-domain tuning: Current implementation applies globally; could be extended for per-resource settings
- Parameter constraints: Some combinations may not be optimal; tuning guidance/presets could be added

VERIFICATION STEPS PERFORMED
-----------------------------
✓ TLV struct sizes validated (8, 24, 8, 4 bytes respectively)
✓ Network byte order conversions verified
✓ Management SET/GET dispatch handlers added
✓ CLI parsing in pmc_common.c for all new parameters
✓ pmc datalen entries added for all new IDs
✓ pmc.c display formatting added for output readability
✓ Compilation verified with pi.h include in clock.c
✓ Commit amended to include all necessary fixes

USAGE SUMMARY
-------------

Get current parameters:
  pmc -d 0 get SERVO_SETTINGS_NP
  pmc -d 0 get PI_CONSTANTS_NP
  pmc -d 0 get TSPROC_FILTER_NP
  pmc -d 0 get CLOCK_FREQ_EST_NP

Set/tune parameters:
  pmc -d 0 set SERVO_SETTINGS_NP numOffsetValues 10 offsetThreshold 200000
  pmc -d 0 set PI_CONSTANTS_NP kp 0.8 ki 0.4 interval 1.1
  pmc -d 0 set TSPROC_FILTER_NP filter_type 1 filter_length 12
  pmc -d 0 set CLOCK_FREQ_EST_NP freq_est_interval 512

Monitor impact:
  - Check ptp4l offset statistics
  - Verify lock convergence time
  - Compare frequency stability before/after tuning
  - Watch for anomalies in logs with -l 5

REFERENCES
----------
- IEEE 1588-2008: PTP Management Messages and TLVs
- linuxptp Management Interface: pmc_common.c, tlv.c
- Servo API: servo.h, servo.c, pi.c

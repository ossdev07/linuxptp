ADAPTIVE RUNTIME TUNING - Testing & Field Deployment Guide
==========================================================

TABLE OF CONTENTS
-----------------
1. Test Process & Validation Strategy
2. Field Deployment Scenarios
3. Troubleshooting & Optimization Guide
4. Risk Mitigation & Rollback Procedures
5. Monitoring & Observability

===============================================================================
PART 1: TEST PROCESS & VALIDATION STRATEGY
===============================================================================

TEST HIERARCHY
--------------

Level 1: Struct/API Validation (No Daemon Required)
Level 2: Integration Testing (Daemon + PMC Client)
Level 3: Field Simulation Testing (Real Network Conditions)
Level 4: Production Pilot Deployment (Controlled Roll-out)

---

LEVEL 1: STRUCT & BYTE-ORDER VALIDATION
========================================

Purpose: Verify TLV definitions and encoding before integration testing

Command:
  $ python validate_runtime_tuning.py

What it validates:
  ✓ Struct sizes match C definitions (8/24/8/4 bytes)
  ✓ Network byte-order conversions (NTOHL, host2net64)
  ✓ Parameter ranges are reasonable
  ✓ Endianness correctness on host platform

Expected Output:
  === Struct Size Validation ===
    servo_settings_np       8 bytes (expected 8)    ... ✓ PASS
    pi_constants_np        24 bytes (expected 24)   ... ✓ PASS
    tsproc_filter_np        8 bytes (expected 8)    ... ✓ PASS
    clock_freq_est_np       4 bytes (expected 4)    ... ✓ PASS

  === Byte Order (Network Endianness) ===
    ServoSettingsNp: ...
      -> packed: 0x00000100...  (big-endian verified)
      -> unpacked: ServoSettingsNp(...)
      ✓ PASS
    [... 3 more ...]

  === Summary ===
    Struct Sizes           ✓ PASS
    Byte Order             ✓ PASS
    Parameter Ranges       ✓ PASS

  ✓ All validation tests PASSED

Why this matters:
  - Catches endianness/platform issues early (before field deployment)
  - Verifies struct packing on your target platform
  - No runtime dependencies (runs on any system with Python 3)
  - Fast feedback on payload correctness

---

LEVEL 2: INTEGRATION TESTING (Daemon + PMC)
============================================

Purpose: Verify management TLV dispatch, PMC CLI parsing, and runtime updates

Prerequisites:
  1. Linux system (or VM) with gcc
  2. ptp4l built: make
  3. pmc tool available
  4. NIC with PTP support (or use UDS for testing)

Test Setup:

  Terminal 1 - Start ptp4l with management interface:
  -------------------------------------------------------
  $ ptp4l -i eth0 -m -l 5 --config=default.cfg
  
  or if no NIC available, use UDS (Unix Domain Socket):
  
  $ cat > test.cfg << 'EOF'
  [global]
  management_enabled 1
  
  [eth0]
  network_transport udp
  EOF
  
  $ ptp4l -i eth0 -m -l 5 --config=test.cfg &
  
  Flags explained:
  -i eth0          : Interface to sync (can be dummy interface)
  -m               : Enable management interface
  -l 5             : Log level 5 (detailed: events + debug)
  --config=cfg     : Use config file

  What to observe:
  - "management incoming ..." messages when pmc sends requests
  - "management set ..." when parameters are updated
  - No errors or segfaults from the new code paths

  Terminal 2 - Run PMC integration test:
  -------------------------------------------------------
  $ bash test_runtime_tuning.sh 0

Test Script Breakdown:
  ╔═══════════════════════════════════════════════════════════════════╗
  ║                  test_runtime_tuning.sh                          ║
  ╠═══════════════════════════════════════════════════════════════════╣
  ║ Test 1: SERVO_SETTINGS_NP                                        ║
  ║   [GET] SERVO_SETTINGS_NP                                        ║
  ║   [SET] SERVO_SETTINGS_NP numOffsetValues 8 offsetThreshold ...  ║
  ║   [VERIFY] SERVO_SETTINGS_NP (confirm values changed)            ║
  ║   Validates: Field updates, GET/SET round-trip                   ║
  ║   Time: 3-5 seconds                                              ║
  ╠───────────────────────────────────────────────────────────────────╣
  ║ Test 2: PI_CONSTANTS_NP                                          ║
  ║   [GET] PI_CONSTANTS_NP                                          ║
  ║   [SET] PI_CONSTANTS_NP kp 0.75 ki 0.35 interval 1.2             ║
  ║   [VERIFY] PI_CONSTANTS_NP (verify floating-point precision)     ║
  ║   Validates: Double-precision encoding/decoding                  ║
  ║   Time: 3-5 seconds                                              ║
  ╠───────────────────────────────────────────────────────────────────╣
  ║ Test 3: TSPROC_FILTER_NP                                         ║
  ║   [GET] TSPROC_FILTER_NP                                         ║
  ║   [SET] TSPROC_FILTER_NP filter_type 0 filter_length 10          ║
  ║   [VERIFY] TSPROC_FILTER_NP (check filter rebuild)               ║
  ║   Validates: Filter dynamic reconstruction                       ║
  ║   Time: 3-5 seconds                                              ║
  ╠───────────────────────────────────────────────────────────────────╣
  ║ Test 4: CLOCK_FREQ_EST_NP                                        ║
  ║   [GET] CLOCK_FREQ_EST_NP                                        ║
  ║   [SET] CLOCK_FREQ_EST_NP freq_est_interval 512                  ║
  ║   [VERIFY] CLOCK_FREQ_EST_NP                                     ║
  ║   Validates: Frequency estimation window updates                 ║
  ║   Time: 3-5 seconds                                              ║
  ╠───────────────────────────────────────────────────────────────────╣
  ║ Test 5: PORT_CORRECTIONS_NP (existing, should still work)        ║
  ║   [GET] PORT_CORRECTIONS_NP                                      ║
  ║   [SET] PORT_CORRECTIONS_NP egressLatency 0 ... delayAsymmetry..║
  ║   [VERIFY] PORT_CORRECTIONS_NP                                   ║
  ║   Validates: Backward compatibility, delay asymmetry override    ║
  ║   Time: 3-5 seconds                                              ║
  ╠───────────────────────────────────────────────────────────────────╣
  ║ Test 6: Rapid Changes (Stress Test)                              ║
  ║   Loop 5 times: pmc set SERVO_SETTINGS_NP numOffsetValues X...  ║
  ║   [VERIFY] Final value persists                                  ║
  ║   Validates: No resource leaks, handles rapid updates            ║
  ║   Time: 10-15 seconds                                            ║
  ╠───────────────────────────────────────────────────────────────────╣
  ║ Test 7: Reset to Defaults                                        ║
  ║   [SET] all parameters back to defaults                          ║
  ║   [VERIFY] values reset                                          ║
  ║   Validates: Clean reset capability                              ║
  ║   Time: 10-15 seconds                                            ║
  ╚═══════════════════════════════════════════════════════════════════╝

  Total Test Duration: 40-60 seconds

Expected Results (Success Criteria):
  ✓ All PMC commands return immediately (< 1 second each)
  ✓ GET responses show current values
  ✓ SET commands confirm receipt
  ✓ VERIFY shows updated values (not changed across GET/SET cycle)
  ✓ No "failed to send" or "implicit declaration" errors
  ✓ ptp4l continues running with no segfaults
  ✓ Synchronization quality unaffected by parameter changes
  ✓ Rapid changes handled without resource leaks

What to Check in ptp4l Logs:
  LOG_LEVEL=5 will show:
  
  [ptp4l.0.message] 06:30:45.123 port 1 received management packet
  [ptp4l.0.message] 06:30:45.124 management set SERVO_SETTINGS_NP
  [ptp4l.0.message] 06:30:45.124 servo updated: numOffsetValues=8

Post-Test Inspection:
  1. Check offset statistics: Should remain stable (no spikes)
  2. Verify frequency estimate: Should converge normally
  3. Monitor memory: ps shows stable RSS (no leaks)
  4. Review logs: No errors or warnings

---

LEVEL 3: FIELD SIMULATION TESTING
==================================

Purpose: Verify tuning effectiveness under realistic network conditions

Scenario Setup:
  - Master clock on network (or simulator)
  - Slave running ptp4l with tuning capability
  - Network conditions: variable delay, jitter, packet loss
  - Duration: 30-60 minutes per scenario

Test Scenarios:

  SCENARIO A: Clean LAN (Best Case)
  ──────────────────────────────────
  Conditions:
    - Low latency (< 1 ms round-trip)
    - Low jitter (< 100 µs standard deviation)
    - No packet loss
    - High sync rate (1 sync/sec)

  Baseline Tuning:
    numOffsetValues: 5 (conservative)
    offsetThreshold: 100000 ns (standard)
    kp: 0.7, ki: 0.3 (default PI)
    filter_length: 8
    freq_est_interval: 256

  Expected Results:
    - Lock time: 10-30 seconds
    - Offset: ±100 ns (after lock)
    - Frequency error: < 1 ppm
    - No divergence over 1 hour

  Optimized Tuning:
    numOffsetValues: 4 (faster response)
    offsetThreshold: 50000 ns (more aggressive)
    kp: 0.9, ki: 0.4 (faster convergence)
    filter_length: 6 (less smoothing needed)
    freq_est_interval: 128 (track faster)

  Apply via PMC:
    $ pmc -d 0 set SERVO_SETTINGS_NP numOffsetValues 4 offsetThreshold 50000
    $ pmc -d 0 set PI_CONSTANTS_NP kp 0.9 ki 0.4 interval 1.0
    $ pmc -d 0 set TSPROC_FILTER_NP filter_type 0 filter_length 6
    $ pmc -d 0 set CLOCK_FREQ_EST_NP freq_est_interval 128

  Monitor & Verify:
    - Lock time: Should improve (target: 5-15 seconds)
    - Offset variance: May decrease (target: ±50 ns)
    - Frequency stability: Should remain good
    - No instability or oscillation

  Success Criteria:
    ✓ Faster convergence without sacrificing stability
    ✓ Tighter offset band
    ✓ No divergence or oscillation
    ✓ ptp4l continues without errors


  SCENARIO B: WAN with Jitter (Typical Case)
  ──────────────────────────────────────────
  Conditions:
    - Variable latency (5-50 ms round-trip)
    - High jitter (1-5 ms standard deviation)
    - Occasional packet loss (0.1-1%)
    - Lower sync rate (1 sync/10 seconds or configurable)

  Baseline Tuning (Conservative):
    numOffsetValues: 8 (smooth out jitter)
    offsetThreshold: 200000 ns (conservative step)
    kp: 0.5, ki: 0.2 (slow convergence)
    filter_length: 16 (heavy smoothing)
    freq_est_interval: 512 (stable estimate)

  Expected Results:
    - Lock time: 60-300 seconds
    - Offset: ±1 µs (acceptable for WAN)
    - Frequency error: < 10 ppm
    - Occasional brief divergences on packet loss

  Optimized Tuning (Balanced):
    numOffsetValues: 6 (moderate smoothing)
    offsetThreshold: 150000 ns (balanced)
    kp: 0.65, ki: 0.30 (faster but stable)
    filter_length: 12 (balanced filtering)
    freq_est_interval: 384 (moderate)

  Apply via PMC (after 5 minutes baseline):
    $ pmc -d 0 set SERVO_SETTINGS_NP numOffsetValues 6 offsetThreshold 150000
    $ pmc -d 0 set PI_CONSTANTS_NP kp 0.65 ki 0.30 interval 1.0
    $ pmc -d 0 set TSPROC_FILTER_NP filter_type 0 filter_length 12
    $ pmc -d 0 set CLOCK_FREQ_EST_NP freq_est_interval 384

  Monitor & Verify:
    - Convergence: Faster without increased divergence risk
    - Jitter rejection: Improved filtering without over-smoothing
    - Packet loss recovery: Quicker return to lock
    - Frequency stability: Maintained or improved

  Success Criteria:
    ✓ 30% faster convergence
    ✓ Offset variance reduced by 20%
    ✓ No increase in lock/unlock cycles
    ✓ Stable frequency estimate despite jitter


  SCENARIO C: Hostile Network (Stress Test)
  ──────────────────────────────────────────
  Conditions:
    - High jitter (5-20 ms standard deviation)
    - Packet loss (5-20%)
    - Intermittent master unavailability
    - Highly variable latency (100 ms swing possible)

  Test Objective:
    Verify robustness; tuning may not achieve full synchronization,
    but should not cause oscillation, divergence, or daemon crashes.

  Conservative Tuning (Defensive):
    numOffsetValues: 10 (maximum smoothing)
    offsetThreshold: 300000 ns (minimal adjustments)
    kp: 0.3, ki: 0.1 (very slow convergence)
    filter_length: 20 (heavy filtering)
    freq_est_interval: 1024 (very stable)

  Expected Results:
    - Sync may not achieve lock
    - Offset: ±5 µs or more (best effort)
    - No daemon crashes or resource leaks
    - Graceful degradation

  Success Criteria:
    ✓ ptp4l continues running
    ✓ No memory leaks or file descriptor exhaustion
    ✓ Handles parameter changes even under poor conditions
    ✓ Logs show clear status (not synced, etc.)


---

===============================================================================
PART 2: FIELD DEPLOYMENT SCENARIOS
===============================================================================

REAL-WORLD USE CASES
====================

Use Case 1: Network Upgrade/Migration
──────────────────────────────────────

Scenario:
  A telecom operator migrates from a legacy timing network to a modern
  datacenter-based PTP infrastructure. Initial parameters are set conservatively
  to avoid any risk, but field tests reveal the new network is extremely clean.

Before Adaptive Tuning:
  - Conservative default parameters
  - Lock time: 120 seconds
  - Offset variance: ±500 ns
  - Operators must wait for stabilization before service activation
  - Risk: Restarting ptp4l would disrupt service (not an option)

With Adaptive Runtime Tuning:
  1. Deploy new ptp4l with old parameters (safe baseline)
  2. Monitor network characteristics for 30 minutes
  3. Observe: low jitter, stable master, fast convergence possible
  4. NO RESTART NEEDED - Simply apply optimized parameters via PMC:

     $ pmc -d 0 set SERVO_SETTINGS_NP numOffsetValues 4 offsetThreshold 50000
     $ pmc -d 0 set PI_CONSTANTS_NP kp 0.85 ki 0.35 interval 1.0
     $ pmc -d 0 set TSPROC_FILTER_NP filter_type 0 filter_length 6
     $ pmc -d 0 set CLOCK_FREQ_EST_NP freq_est_interval 128

  5. Within seconds: New parameters take effect
     - Lock time: now 30 seconds
     - Offset variance: ±100 ns
     - Service quality improved without downtime

  Result:
    ✓ 75% faster synchronization
    ✓ Zero service interruption
    ✓ No configuration file edits or daemon restarts
    ✓ Instant rollback possible if issues arise


Use Case 2: Handling Weather/Environmental Changes
───────────────────────────────────────────────────

Scenario:
  A financial institution runs PTP over long-haul fiber connections.
  Summer heat causes increased thermal jitter in fiber plant.

Before Adaptive Tuning:
  - Fixed parameters for all seasons
  - Summer: Increased offset variance, reduced accuracy
  - Only option: Restart daemon with new config (requires maintenance window)
  - Risk: Customer-facing downtime during market hours

With Adaptive Runtime Tuning:
  1. Summer conditions detected (offset variance increases above threshold)
  2. Operator or automated script issues PMC command to increase filtering:

     $ pmc -d 0 set TSPROC_FILTER_NP filter_type 0 filter_length 14

  3. Within milliseconds: Filter window increased
     - Additional timestamp smoothing applied
     - Offset variance reduced back to acceptable range

  4. Monitoring daemon watches metrics; if they degrade further:

     $ pmc -d 0 set SERVO_SETTINGS_NP offsetThreshold 180000
     $ pmc -d 0 set PI_CONSTANTS_NP kp 0.6 ki 0.25 interval 1.0

  5. Fall arrives, conditions improve:

     $ pmc -d 0 set TSPROC_FILTER_NP filter_type 0 filter_length 8
     $ # Revert to normal parameters

  Result:
    ✓ Automatic adaptation to environmental changes
    ✓ No downtime or service disruption
    ✓ Better accuracy year-round
    ✓ Reduced need for seasonal reconfigurations


Use Case 3: Quick Problem Diagnosis & Recovery
───────────────────────────────────────────────

Scenario:
  A utility company operates critical power grid synchronization.
  A temporary fiber cut causes rerouting through a congested path.
  Synchronization accuracy degrades; operators have minutes to respond.

Before Adaptive Tuning:
  - Degraded lock: offset ±5 µs
  - Options: (1) Wait for network recovery, (2) Restart daemon with emergency config
  - (1) is passive/risky, (2) causes brief outage and risk of instability during restart

With Adaptive Runtime Tuning:
  1. Operator observes degradation via monitoring dashboard
  2. Quickly issues diagnostic GET:

     $ pmc -d 0 get SERVO_SETTINGS_NP
     $ pmc -d 0 get PI_CONSTANTS_NP
     $ pmc -d 0 get TSPROC_FILTER_NP

  3. Current parameters show: numOffsetValues=5, kp=0.7 (not aggressive enough for congestion)
  
  4. Apply emergency tuning (conservative, to survive poor conditions):

     $ pmc -d 0 set SERVO_SETTINGS_NP numOffsetValues 10 offsetThreshold 300000
     $ pmc -d 0 set TSPROC_FILTER_NP filter_type 0 filter_length 16
     $ pmc -d 0 set PI_CONSTANTS_NP kp 0.4 ki 0.15 interval 1.0

  5. Offset stabilizes: ±1 µs (acceptable, if not ideal)
  6. System remains operational during network recovery
  7. When network improves, revert to normal parameters:

     $ pmc -d 0 set SERVO_SETTINGS_NP numOffsetValues 5 offsetThreshold 100000
     $ pmc -d 0 set TSPROC_FILTER_NP filter_type 0 filter_length 8
     $ pmc -d 0 set PI_CONSTANTS_NP kp 0.7 ki 0.3 interval 1.0

  Result:
    ✓ Quick response (seconds) without downtime
    ✓ Graceful degradation vs. hard failure
    ✓ Zero risk from daemon restart
    ✓ Full recovery when conditions improve


Use Case 4: A/B Testing & Performance Tuning
─────────────────────────────────────────────

Scenario:
  A semiconductor fab wants to optimize PTP accuracy for wafer processing
  timing. Engineering team proposes new tuning parameters to reduce lock time.

Traditional Approach:
  - Edit config file with new parameters
  - Restart ptp4l (brief outage)
  - Run tests
  - If problems arise, restart again with old config
  - Iterative testing takes hours/days due to restart overhead

With Adaptive Runtime Tuning:
  1. Establish baseline metrics (run 10 minutes, log offset/frequency stats)
  2. Apply tuning variant A (e.g., kp=0.8, ki=0.4):

     $ pmc -d 0 set PI_CONSTANTS_NP kp 0.8 ki 0.4 interval 1.0

  3. Monitor for 10 minutes, collect metrics
  4. Apply tuning variant B (e.g., kp=0.85, ki=0.35):

     $ pmc -d 0 set PI_CONSTANTS_NP kp 0.85 ki 0.35 interval 1.0

  5. Monitor for 10 minutes, collect metrics
  6. Revert to baseline if needed:

     $ pmc -d 0 set PI_CONSTANTS_NP kp 0.7 ki 0.3 interval 1.0

  7. Compare results from all variants (no downtime for any test)
  8. Deploy optimal tuning permanently

  Result:
    ✓ Rapid A/B testing without downtime
    ✓ Compare 5+ variants in 60 minutes (instead of hours)
    ✓ Confident selection of best parameters
    ✓ No risk of instability during testing


Use Case 5: Distributed Rollout & Progressive Deployment
─────────────────────────────────────────────────────────

Scenario:
  A multinational bank deploys new PTP-based transaction timing across 50 offices.
  Central team wants to validate new servo tuning on a subset before global rollout.

Traditional Approach:
  - Test thoroughly in lab on identical hardware
  - Deploy to all 50 sites simultaneously (risky)
  - If issues arise, restart daemons everywhere (coordinated outage)
  - Rollback or fix requires another coordinated restart

With Adaptive Runtime Tuning:
  Phase 1: Deploy to 2 pilot offices
    - Use conservative parameters (baseline)
    - Collect data for 1 week
    - Validate no issues

  Phase 2: Deploy to 10 offices with proposed tuning
    - Activate proposed parameters via PMC at all 10:

      $ for office in 1..10; do
          ssh office${office}.timingserver \
          pmc -d 0 set PI_CONSTANTS_NP kp 0.8 ki 0.4 interval 1.0
        done

    - Monitor metrics across all 10
    - Collect data for 1 week
    - If ANY issue observed, revert immediately (no restart):

      $ for office in 1..10; do
          ssh office${office}.timingserver \
          pmc -d 0 set PI_CONSTANTS_NP kp 0.7 ki 0.3 interval 1.0
        done

  Phase 3: Gradual rollout
    - Deploy tuning to 5 more offices per day
    - Revert any problematic site in seconds
    - Full confidence before rolling to all 50

  Phase 4: Global deployment
    - Apply to all 50 offices in parallel:

      $ for office in 1..50; do
          ssh office${office}.timingserver \
          pmc -d 0 set PI_CONSTANTS_NP kp 0.8 ki 0.4 interval 1.0 &
        done

    - Full deployment takes minutes
    - Validation via monitoring dashboard
    - Instant rollback if needed

  Result:
    ✓ Safe progressive deployment
    ✓ Fast rollback if issues arise
    ✓ Minimal risk to mission-critical infrastructure
    ✓ Confidence through incremental validation


---

===============================================================================
PART 3: TROUBLESHOOTING & OPTIMIZATION GUIDE
===============================================================================

PROBLEM DIAGNOSIS
=================

Issue: PMC command fails with "failed to send SET"
─────────────────────────────────────────────────

Causes:
  1. ptp4l not running
  2. Management interface not enabled
  3. Wrong port number (-d flag)
  4. UDS socket path mismatch

Diagnosis:
  $ pgrep ptp4l
  # Should return process ID; if nothing, ptp4l not running

  $ pmc -d 0 get DEFAULT_DATA_SET
  # If this works, management interface is OK

  $ ls -la /var/run/ptp4l*
  # Check socket files exist

Resolution:
  1. Start ptp4l with -m flag: ptp4l -i eth0 -m -l 5
  2. Verify socket: ls /var/run/ptp4l (or configured path)
  3. Try same port number in pmc: pmc -d 0 get DEFAULT_DATA_SET
  4. Check logs: ptp4l should show "management incoming ..." messages


Issue: Parameters changed, but synchronization not improved
──────────────────────────────────────────────────────────

Causes:
  1. Master clock too unstable (tuning can't fix upstream issues)
  2. Network too jittery (parameters insufficient)
  3. Parameter change didn't take effect (verify with GET)
  4. Expected improvement unrealistic for network conditions

Diagnosis:
  $ pmc -d 0 get SERVO_SETTINGS_NP
  # Verify new values are reported back

  $ ptp4l -l 5
  # Monitor logs for: "servo: offset N, freq ±M"
  # After setting new parameters, should see values change within 1-2 sync intervals

  $ pmc -d 0 get DEFAULT_DATA_SET
  # Check master quality (offsetFromMaster, master priority)

Resolution:
  1. Confirm parameters changed: GET each TLV after SET
  2. Monitor logs for 5-10 minutes after change
  3. Verify master is stable (not changing clock IDs)
  4. Try more aggressive tuning if network allows:
     - Increase numOffsetValues (more smoothing)
     - Decrease filter_length (if too much filtering)
     - OR more conservative if seeing oscillation:
       - Decrease numOffsetValues
       - Increase filter_length
       - Lower PI gains (reduce kp/ki)

  5. If improvement marginal, network conditions may be limiting factor


Issue: Synchronization oscillates or diverges after tuning
──────────────────────────────────────────────────────────

Causes:
  1. PI gains too aggressive (kp/ki too high)
  2. numOffsetValues too low (insufficient smoothing)
  3. Servo interval mismatch (interval parameter wrong)
  4. Network instability coinciding with parameter change

Diagnosis:
  $ pmc -d 0 get PI_CONSTANTS_NP
  # Check kp, ki values

  $ ptp4l -l 5 | grep "servo:"
  # Watch offset behavior: Should be bounded; if diverging, tuning issue

  $ pmc -d 0 get SERVO_SETTINGS_NP
  # Check numOffsetValues (too low = noisy)

Resolution:
  Immediate: Revert to conservative parameters:
  
  $ pmc -d 0 set PI_CONSTANTS_NP kp 0.5 ki 0.2 interval 1.0
  $ pmc -d 0 set SERVO_SETTINGS_NP numOffsetValues 8 offsetThreshold 100000
  $ pmc -d 0 set TSPROC_FILTER_NP filter_type 0 filter_length 12

  Then gradually increase aggressiveness:
  
  $ pmc -d 0 set PI_CONSTANTS_NP kp 0.6 ki 0.25 interval 1.0
  # Test for 10 minutes
  
  $ pmc -d 0 set PI_CONSTANTS_NP kp 0.65 ki 0.30 interval 1.0
  # Test for 10 minutes
  
  Continue in small steps until oscillation appears, then back off.


---

OPTIMIZATION WORKFLOW
=====================

Step 1: Collect Baseline Metrics (30 minutes)
─────────────────────────────────────────────

With default parameters, gather:

  $ pmc -d 0 get DEFAULT_DATA_SET > baseline_ds.txt
  $ # Watch ptp4l logs, record:
  #   - Time to first lock
  #   - Typical offset after lock: ±100 ns (example)
  #   - Frequency estimate stability
  
  Run for 30 minutes, then save offset statistics.


Step 2: Profile Network Characteristics
───────────────────────────────────────

Measure:
  - Sync message inter-arrival time (should match config)
  - Delay variations (min/max/stdev)
  - Master stability (doesn't change during test)
  
Tools:
  $ tcpdump -i eth0 'udp port 320' -vv
  # Capture PTP packets; inspect delay variations
  
  $ pmc -d 0 get PORT_DATA_SET
  # Check neighbor delay, sync interval


Step 3: Determine Tuning Direction
──────────────────────────────────

Based on characteristics:

  Clean Network (LAN):
    → Increase aggressiveness
    → Lower numOffsetValues, lower filter_length
    → Higher PI gains (kp/ki)
    → Shorter freq_est_interval

  Jittery Network (WAN):
    → Increase smoothing
    → Higher numOffsetValues, higher filter_length
    → Lower PI gains
    → Longer freq_est_interval


Step 4: Apply & Test Variants
──────────────────────────────

Test conservative variant first:

  $ pmc -d 0 set SERVO_SETTINGS_NP numOffsetValues 6 offsetThreshold 120000
  # Wait 5 minutes
  $ # Record offset statistics

Then test balanced:

  $ pmc -d 0 set SERVO_SETTINGS_NP numOffsetValues 5 offsetThreshold 100000
  # Wait 5 minutes
  $ # Record offset statistics

Then test aggressive:

  $ pmc -d 0 set SERVO_SETTINGS_NP numOffsetValues 4 offsetThreshold 80000
  # Wait 5 minutes
  $ # Record offset statistics


Step 5: Compare & Select
─────────────────────────

Analyze collected data:
  - Lock time: Faster is better (if stability maintained)
  - Offset variance (stdev): Lower is better
  - Frequency error: Should remain < 1 ppm

Select tuning that:
  - Achieves target offset accuracy
  - Converges in acceptable time
  - Remains stable (no oscillation/divergence)
  - Handles occasional packet loss gracefully


Step 6: Validate on Production Replica
───────────────────────────────────────

If possible, test optimal tuning on:
  - Identical hardware
  - Same network topology/conditions
  - Full 24+ hour validation run
  - Monitor for any corner-case issues


Step 7: Deploy Gradually
─────────────────────────

Phase approach:
  1. Pilot: 1-2 systems, 1 week
  2. Limited rollout: 10% of systems, 1 week
  3. Gradual rollout: 25% → 50% → 75% → 100% (weekly)
  4. Retain ability to rollback at any phase


Step 8: Capture & Document
───────────────────────────

Record final tuning:
  
  $ cat > /etc/ptp_tuning_template.sh << 'EOF'
  #!/bin/bash
  # Optimal tuning for production network
  pmc -d 0 set SERVO_SETTINGS_NP numOffsetValues 5 offsetThreshold 100000
  pmc -d 0 set PI_CONSTANTS_NP kp 0.7 ki 0.3 interval 1.0
  pmc -d 0 set TSPROC_FILTER_NP filter_type 0 filter_length 8
  pmc -d 0 set CLOCK_FREQ_EST_NP freq_est_interval 256
  EOF
  
  chmod +x /etc/ptp_tuning_template.sh

Document in runbook:
  - Rationale: Why these parameters for this network
  - Baseline metrics: Expected performance
  - Monitoring: Key metrics to watch
  - Rollback: How to revert if issues arise


---

===============================================================================
PART 4: RISK MITIGATION & ROLLBACK PROCEDURES
===============================================================================

RISKS & MITIGATIONS
===================

Risk 1: Parameter Setting Causes Instability
─────────────────────────────────────────────

Manifestation:
  - Offset oscillates (±5 µs swings)
  - Servo diverges (offset grows indefinitely)
  - Lock/unlock cycles (rapid switching)

Mitigation:
  ✓ Always test on non-production system first
  ✓ Use small parameter increments (not large jumps)
  ✓ Monitor immediately after change (first 5-10 minutes)
  ✓ Have instant rollback procedure ready
  ✓ Conservative default parameters (start safe)
  ✓ Gradual deployment (don't apply to all systems at once)

Rollback (Immediate):
  $ pmc -d 0 set PI_CONSTANTS_NP kp 0.7 ki 0.3 interval 1.0
  $ pmc -d 0 set SERVO_SETTINGS_NP numOffsetValues 5 offsetThreshold 100000
  $ pmc -d 0 set TSPROC_FILTER_NP filter_type 0 filter_length 8
  
  Takes < 1 second; synchronization should stabilize within 1-2 sync intervals


Risk 2: Tuning Inadvertently Breaks Compliance
───────────────────────────────────────────────

Manifestation:
  - Frequency error exceeds spec (> 100 ppm)
  - Offset exceeds service requirement (e.g., requires ±1 µs but get ±10 µs)
  - Clock accuracy certification lost

Mitigation:
  ✓ Understand accuracy requirements before tuning
  ✓ Test against compliance benchmarks
  ✓ Validate frequency stability after tuning
  ✓ Document compliance targets in runbook
  ✓ Conservative defaults for critical systems

Validation Check:
  After any tuning change:
  1. Monitor offset for 30 minutes
  2. Check: max(|offset|) < requirement
  3. Check: frequency error < requirement
  4. If not met, revert immediately


Risk 3: Operator Error (Wrong Parameter Values)
────────────────────────────────────────────────

Manifestation:
  - Typo: pmc -d 0 set SERVO_SETTINGS_NP numOffsetValues 100000
    (sets to insane value, servo becomes very sluggish)
  - Unit mismatch: Expects microseconds but system uses nanoseconds

Mitigation:
  ✓ Validate inputs before sending PMC command
  ✓ Create wrapper script with predefined tuning profiles:

    $ cat > /usr/local/bin/ptp-tune.sh << 'EOF'
    #!/bin/bash
    case "$1" in
      clean-lan)
        pmc -d 0 set SERVO_SETTINGS_NP numOffsetValues 4 offsetThreshold 50000
        pmc -d 0 set PI_CONSTANTS_NP kp 0.85 ki 0.35 interval 1.0
        ;;
      wan-default)
        pmc -d 0 set SERVO_SETTINGS_NP numOffsetValues 6 offsetThreshold 120000
        pmc -d 0 set PI_CONSTANTS_NP kp 0.65 ki 0.30 interval 1.0
        ;;
      reset)
        pmc -d 0 set SERVO_SETTINGS_NP numOffsetValues 5 offsetThreshold 100000
        pmc -d 0 set PI_CONSTANTS_NP kp 0.7 ki 0.3 interval 1.0
        ;;
      *)
        echo "Usage: $0 {clean-lan|wan-default|reset}"
        exit 1
        ;;
    esac
    EOF
    
    chmod +x /usr/local/bin/ptp-tune.sh

  ✓ Operators run: ptp-tune.sh clean-lan (no typos possible)
  ✓ Only approved profiles available


Risk 4: Tuning Lost on Daemon Restart
──────────────────────────────────────

Manifestation:
  - Daemon crashes/restarts
  - System reboot
  - ptp4l service restarted
  → All tuning reverts to defaults

Mitigation (Option 1: Config File):
  $ cat > /etc/ptp4l.conf << 'EOF'
  [global]
  management_enabled 1
  
  # Note: These servo parameters would need to be applied via PMC
  # after daemon startup, as config file does not support them yet.
  EOF

Mitigation (Option 2: Init Script):
  $ cat > /etc/rc.d/rc.ptp4l-tune << 'EOF'
  #!/bin/bash
  # Run after ptp4l starts to apply production tuning
  
  sleep 5  # Wait for daemon to start
  
  pmc -d 0 set SERVO_SETTINGS_NP numOffsetValues 5 offsetThreshold 100000
  pmc -d 0 set PI_CONSTANTS_NP kp 0.7 ki 0.3 interval 1.0
  pmc -d 0 set TSPROC_FILTER_NP filter_type 0 filter_length 8
  pmc -d 0 set CLOCK_FREQ_EST_NP freq_est_interval 256
  EOF
  
  # Call this from ptp4l systemd service or init script

Mitigation (Option 3: Systemd):
  $ cat > /etc/systemd/system/ptp4l-tune.service << 'EOF'
  [Unit]
  Description=Apply PTP Servo Tuning
  After=ptp4l.service
  
  [Service]
  Type=oneshot
  ExecStart=/usr/local/bin/ptp-tune.sh production
  RemainAfterExit=yes
  
  [Install]
  WantedBy=multi-user.target
  EOF


Risk 5: Parameter Inconsistency Across Systems
───────────────────────────────────────────────

Manifestation:
  - Different tuning applied to different nodes
  - Some nodes converge faster, causing time skew in cluster
  - Coordination issues between systems

Mitigation:
  ✓ Use centralized configuration/orchestration:
  
    $ for host in ptp-master ptp-slave{1..10}; do
        ssh $host /usr/local/bin/ptp-tune.sh clean-lan
      done
  
  ✓ Verify consistency after deployment:
  
    $ for host in ptp-master ptp-slave{1..10}; do
        echo "=== $host ==="
        ssh $host "pmc -d 0 get SERVO_SETTINGS_NP"
      done

  ✓ Document expected values in runbook


---

ROLLBACK PROCEDURES
===================

Scenario: All Systems
─────────────────────

If global rollback needed (tuning causes widespread issues):

  Step 1: Stop accepting new tuning requests
    $ # Disable automated tuning scripts
    $ touch /var/lock/ptp-tuning-disabled

  Step 2: Revert all systems to defaults
    $ for host in ptp-{master,slave}{1..50}; do
        ssh $host \
        pmc -d 0 set PI_CONSTANTS_NP kp 0.7 ki 0.3 interval 1.0 \
        && ssh $host \
        pmc -d 0 set SERVO_SETTINGS_NP numOffsetValues 5 offsetThreshold 100000
      done

  Step 3: Monitor convergence
    $ # Watch logs on each host for "locked to master"
    $ # Should take 30-60 seconds

  Step 4: Validate synchronization restored
    $ for host in ptp-{master,slave}{1..50}; do
        ssh $host "pmc -d 0 get DEFAULT_DATA_SET | grep Offset"
      done
    $ # All should show offset within ±1 µs

  Step 5: Post-incident review
    $ # Analyze what tuning caused issue
    $ # Did parameter value exceed safe range?
    $ # Was network condition unexpected?
    $ # Update runbook with lessons learned


Scenario: Single System
───────────────────────

If only one system experiencing issues:

  Step 1: Identify problematic system
    $ # Via monitoring dashboard or logs

  Step 2: SSH to affected system
    $ ssh ptp-slave-5

  Step 3: Immediately revert
    $ pmc -d 0 set PI_CONSTANTS_NP kp 0.7 ki 0.3 interval 1.0

  Step 4: Verify recovery
    $ pmc -d 0 get DEFAULT_DATA_SET | grep Offset

  Step 5: Investigate
    $ ptp4l -l 5 | grep servo
    $ # Did offset stabilize? If not, check network


Scenario: Progressive Rollback (Gradual)
─────────────────────────────────────────

If reverting to defaults affects synchronization in reverse:

  Instead of:
    kp 0.7 ki 0.3 (target)
  
  Do staged revert:
    kp 0.77 ki 0.32 (slightly less aggressive)
    kp 0.74 ki 0.31 (middle ground)
    kp 0.70 ki 0.30 (full revert)
  
  Each step: 5 minute hold, verify stability


Emergency Contact Tree
──────────────────────

In case of widespread tuning-related outage:

  1. Incident Commander: On-call Ops lead
  2. Execute immediate rollback (Step 2 above)
  3. Notify: Infrastructure lead, Network team, Customer Success
  4. Analyze root cause
  5. Document findings and prevent recurrence


---

===============================================================================
PART 5: MONITORING & OBSERVABILITY
===============================================================================

METRICS TO MONITOR
===================

Real-time Metrics:
──────────────────

1. Offset from Master
   Command: pmc -d 0 get DEFAULT_DATA_SET | grep "offsetFromMaster"
   
   Target (after tuning):
     Clean LAN:   ±50–100 ns
     WAN:         ±500 ns–1 µs
     Congested:   ±5–10 µs
   
   Alert Threshold:
     If offset > 3× baseline for > 2 minutes → Alert
   
   Interpretation:
     - Increasing offset: Servo lagging (not catching master drift)
     - Oscillating offset: Servo over-tuned (gains too high)
     - Stable offset: Tuning working correctly


2. Frequency Offset
   Command: pmc -d 0 get DEFAULT_DATA_SET | grep "scaledLogVariance"
   
   Target:
     Good sync: < 1 ppm error
     Poor sync: 1–10 ppm
   
   Alert Threshold:
     If frequency error > 10 ppm → Alert (servo not catching master frequency)
   
   Interpretation:
     - Increasing frequency error: Master drifting or servo unable to track
     - Stable frequency: Frequency estimation working


3. Servo State
   Command: ptp4l logs show "servo: offset N, freq ±M"
   
   Monitor:
     - Lock status (locked vs. not synced)
     - Lock time (seconds to converge)
     - Frequency adjustment rate (ns/update)
   
   Interpretation:
     - Frequent lock/unlock cycles: Network too unstable for current tuning
     - Lock time increasing: Servo too conservative
     - Frequency not converging: Master too unstable


4. Network Conditions
   Command: tcpdump or pmc queries
   
   Measure:
     - Sync message interval (should match config)
     - Delay variations (min/max/stdev)
     - Packet loss rate
   
   Interpretation:
     - High jitter: May need higher filter_length
     - Packet loss: May need slower servo (lower gains)


---

ALERTING RULES
==============

Rule 1: Offset Divergence Alert
────────────────────────────────

Condition:
  Offset exceeds baseline by > 300% AND time_in_state > 2 minutes

Action:
  1. Send alert to ops team
  2. Auto-fetch: pmc -d 0 get SERVO_SETTINGS_NP
  3. Log current tuning for investigation
  4. Suggest manual check or automated revert

Automation:
  $ cat > /usr/local/bin/ptp-health-check.sh << 'EOF'
  #!/bin/bash
  BASELINE=100     # nanoseconds
  THRESHOLD_PCT=300
  
  OFFSET=$(pmc -d 0 get DEFAULT_DATA_SET | grep offsetFromMaster | awk '{print $NF}')
  THRESHOLD=$((BASELINE * THRESHOLD_PCT / 100))
  
  if [ ${OFFSET#-} -gt $THRESHOLD ]; then
    echo "ALERT: Offset $OFFSET exceeds threshold $THRESHOLD"
    pmc -d 0 get SERVO_SETTINGS_NP >> /var/log/ptp-alert.log
    # Trigger PagerDuty/Slack/etc.
  fi
  EOF
  
  # Run every 60 seconds via cron:
  # * * * * * /usr/local/bin/ptp-health-check.sh


Rule 2: Tuning Parameter Out-of-Bounds Alert
──────────────────────────────────────────────

Condition:
  Parameter reported by PMC GET differs from expected value

Action:
  1. Log alert with current vs. expected
  2. Auto-correct by sending SET with expected value
  3. If correction fails, escalate

Automation:
  $ cat > /usr/local/bin/ptp-verify-tuning.sh << 'EOF'
  #!/bin/bash
  EXPECTED_KP=0.7
  EXPECTED_KI=0.3
  
  ACTUAL_KP=$(pmc -d 0 get PI_CONSTANTS_NP | grep "kp" | awk '{print $NF}')
  
  if [ "$ACTUAL_KP" != "$EXPECTED_KP" ]; then
    echo "ALERT: kp=$ACTUAL_KP, expected=$EXPECTED_KP"
    # Attempt correction
    pmc -d 0 set PI_CONSTANTS_NP kp $EXPECTED_KP ki $EXPECTED_KI interval 1.0
  fi
  EOF


Rule 3: Daemon Offline but Tuning Needed
─────────────────────────────────────────

Condition:
  ptp4l not responsive to PMC queries for > 5 minutes

Action:
  1. Check if daemon running: pgrep ptp4l
  2. If not running, start: systemctl start ptp4l
  3. Wait for socket, re-apply tuning
  4. Alert if daemon doesn't recover in 30 seconds

Automation:
  $ cat > /usr/local/bin/ptp-daemon-monitor.sh << 'EOF'
  #!/bin/bash
  
  if ! pmc -d 0 get DEFAULT_DATA_SET > /dev/null 2>&1; then
    echo "ERROR: ptp4l not responding"
    
    if pgrep ptp4l > /dev/null; then
      echo "WARNING: ptp4l running but not responding; restarting..."
      systemctl restart ptp4l
    else
      echo "ERROR: ptp4l not running; starting..."
      systemctl start ptp4l
    fi
    
    sleep 5
    
    # Re-apply tuning
    /usr/local/bin/ptp-tune.sh production
  fi
  EOF


---

MONITORING DASHBOARD (Example)
===============================

Display real-time metrics:

  ┌─────────────────────────────────────────────────────────┐
  │ PTP Servo Monitoring Dashboard                          │
  ├─────────────────────────────────────────────────────────┤
  │ Master: 00:11:22:33:44:55                               │
  │ Status: LOCKED                                           │
  │ Offset: +42 ns (target: ±100 ns)  [OK]                 │
  │ Frequency: +0.3 ppm (target: ±1 ppm)  [OK]             │
  │                                                          │
  │ Servo Tuning (Current):                                 │
  │   numOffsetValues: 5                                     │
  │   offsetThreshold: 100000 ns                             │
  │   PI(kp=0.7, ki=0.3)                                    │
  │   filter_length: 8                                      │
  │   freq_est_interval: 256                                │
  │                                                          │
  │ Network Conditions:                                     │
  │   Sync Rate: 1 msg/sec                                  │
  │   Delay Jitter: ±50 µs                                  │
  │   Packet Loss: 0.1%                                     │
  │   Lock Time: 42 seconds (baseline: 45 sec)              │
  │                                                          │
  │ Alerts: None                                             │
  │ Last Tuning Change: 2 hours ago (kp 0.7→0.75→0.7)       │
  └─────────────────────────────────────────────────────────┘

Implement with:
  - Grafana + Prometheus (scrape pmc output)
  - Custom Python script polling pmc every 60 seconds
  - systemd timers logging metrics to file


---

SUMMARY: TESTING & DEPLOYMENT BENEFITS
======================================

Testing Validates:
  ✓ Struct definitions correct for target platform
  ✓ Network byte order conversions work (no endian surprises in field)
  ✓ Management TLV dispatch functions operational
  ✓ PMC CLI parsing handles edge cases
  ✓ Parameters persist across multiple SET/GET cycles
  ✓ Rapid parameter changes don't cause resource leaks
  ✓ Daemon remains stable under stress (rapid tuning changes)
  ✓ Backward compatibility maintained (old TLVs still work)

Field Deployment Benefits:
  ✓ ZERO DOWNTIME: Tune parameters without restart
  ✓ FAST RECOVERY: Adapt to network changes in seconds
  ✓ RISK REDUCTION: Test before deploying (gradual rollout)
  ✓ COMPLIANCE: Maintain accuracy requirements dynamically
  ✓ EFFICIENCY: A/B test tuning variants without downtime
  ✓ RELIABILITY: Graceful degradation on poor conditions
  ✓ AGILITY: Respond to environmental/operational changes rapidly
  ✓ OBSERVABILITY: Monitor and adjust based on real-time metrics

ROI:
  - Reduced maintenance windows (no restarts = reduced ops cost)
  - Improved synchronization quality (better tuning without restart penalty)
  - Faster incident response (tune away problems, no restart needed)
  - Lower risk deployments (test on subset, gradual rollout)
  - Higher availability (maintain service quality during adaptation)

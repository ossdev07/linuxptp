# Adaptive ptp4l Usage and Deployment Guide

This document explains how to run the adaptive `ptp4l` build, how the adaptive
profiles are selected, and how this helps during lab demos and Nokia field
deployments with multiple Grandmasters (GMs).

The adaptive layer is designed to tune ptp4l at runtime based on observed clock
behavior. It can adjust servo settling criteria, PI constants, timestamp
processor filtering, frequency-estimation interval, step thresholds, and maximum
frequency correction. This avoids rebuilding or restarting ptp4l every time the
field condition changes.

## 1. What the Adaptive Layer Does

The adaptive engine monitors recent synchronization samples and classifies the
network condition using:

- offset jitter
- packet or sync loss
- servo stable-state count
- GM change events

Based on those inputs it applies one of three tuning profiles:

| Profile | Use case | Behavior |
|---------|----------|----------|
| `conservative` | Noisy links, high jitter, loss, unstable GM path | More filtering, slower correction, wider offset threshold |
| `balanced` | Normal field condition | Moderate filtering and PI response |
| `aggressive` | Clean lab or stable deterministic path | Faster correction, shorter filtering, tighter offset threshold |

The built-in profile values are:

| Parameter | Conservative | Balanced | Aggressive |
|-----------|--------------|----------|------------|
| `servo_num_offset_values` | 8 | 5 | 4 |
| `servo_offset_threshold` | 200000 ns | 100000 ns | 50000 ns |
| `kp` | 0.5 | 0.7 | 1.0 |
| `ki` | 0.2 | 0.3 | 0.5 |
| `filter_length` | 16 | 10 | 6 |
| `freq_est_interval` | 3 | 2 | 1 |
| `step_threshold` | 20000 ns | 20000 ns | 20000 ns |
| `first_step_threshold` | 20000 ns | 20000 ns | 20000 ns |
| `max_frequency` | 900000000 ppb | 900000000 ppb | 900000000 ppb |

## 2. Basic Configuration

Add these options to the ptp4l configuration file.

```ini
# Enable adaptive runtime tuning.
adap_tuning_enabled 1

# auto means the engine may switch between conservative/balanced/aggressive.
# conservative, balanced, or aggressive means fixed profile mode.
adap_tuning_mode auto

# How often the adaptive engine evaluates recent samples.
adap_eval_interval 1.0

# Number of recent samples used for jitter/loss/stability decisions.
adap_sample_window 10

# Optional per-GM profile file.
adap_gm_profile_file /etc/linuxptp/adap-gm-profiles.csv
```

Recommended baseline for field rollout:

```ini
clock_servo pi
adap_tuning_enabled 1
adap_tuning_mode conservative
adap_eval_interval 2.0
adap_sample_window 15
```

Recommended baseline for lab demo with impairment testing:

```ini
clock_servo pi
adap_tuning_enabled 1
adap_tuning_mode auto
adap_eval_interval 1.0
adap_sample_window 10
```

## 3. Starting ptp4l

Example command for the LS1046 board case:

```bash
./ptp4l -i fm1-mac9 -m -s -f /root/ptp/G.8275.2.cfg
```

Expected startup logs:

```text
selected /dev/ptp1 as PTP clock
ADAP: created adaptive engine, mode=balanced window=10 enabled=1 auto=1
port 1 (fm1-mac9): INITIALIZING to LISTENING on INIT_COMPLETE
```

When a GM is selected:

```text
selected best master clock 00049f.fffe.07ba9b
ADAP: GM changed to 00049f.fffe.07ba9b, using default balanced mode
ADAP: applied params: numOff=5 offThr=100000 kp=0.700 ki=0.300 fltLen=10 freqEst=2 stepThr=20000 maxFreq=900000000
```

## 4. Meaning of `adap_tuning_enabled` and `adap_tuning_mode`

`adap_tuning_enabled 1` only turns the adaptive engine on. It does not by itself
mean that the engine is allowed to switch profiles automatically.

Use this rule:

| Config | Result |
|--------|--------|
| `adap_tuning_enabled 0` | Adaptive layer is disabled. Normal ptp4l behavior. |
| `adap_tuning_enabled 1` + `adap_tuning_mode auto` | Adaptive layer applies profiles and may switch modes at runtime. |
| `adap_tuning_enabled 1` + `adap_tuning_mode conservative` | Adaptive layer applies conservative profile and stays there. |
| `adap_tuning_enabled 1` + `adap_tuning_mode balanced` | Adaptive layer applies balanced profile and stays there. |
| `adap_tuning_enabled 1` + `adap_tuning_mode aggressive` | Adaptive layer applies aggressive profile and stays there. |

For manual `pmc` tuning experiments, avoid `auto` mode. In `auto` mode the
adaptive engine can overwrite manual `pmc SET` values on the next evaluation or
GM change. Use one of these instead:

```ini
adap_tuning_enabled 0
```

or:

```ini
adap_tuning_enabled 1
adap_tuning_mode balanced
```

## 5. Per-GM Profiles for Multi-GM Deployment

In Nokia field deployment there may be multiple GMs serving different systems or
paths. Each GM can have different noise, path delay, packet-loss behavior, and
holdover impact. The adaptive layer supports a GM profile file so that ptp4l can
apply known-good tuning immediately when a specific GM is selected.

Configure:

```ini
adap_gm_profile_file /etc/linuxptp/adap-gm-profiles.csv
```

CSV format:

```text
# gmIdentity,label,numOffsetValues,offsetThreshold,kp,ki,interval,filterLength,freqEstInterval,stepThresholdNs,firstStepThresholdNs,maxFrequencyPpb
00049f.fffe.07ba9b,Nokia-GM-A-clean,4,50000,1.0,0.5,1.0,6,1,20000,20000,900000000
00049f.fffe.065439,Nokia-GM-B-field,8,200000,0.5,0.2,1.0,16,3,20000,20000,900000000
```

Use the linuxptp clock identity format shown in ptp4l logs, for example
`00049f.fffe.07ba9b`.

Expected log when a known GM is selected:

```text
ADAP: GM changed to 00049f.fffe.065439, applying profile 'Nokia-GM-B-field'
ADAP: applied params: numOff=8 offThr=200000 kp=0.500 ki=0.200 fltLen=16 freqEst=3 stepThr=20000 maxFreq=900000000
```

If there is no matching GM profile, ptp4l falls back to the configured mode.

## 6. Runtime Verification with pmc

Use the same domain number that ptp4l is running with. For domain 44:

```bash
pmc -u -d 44 -b 0 "GET PI_CONSTANTS_NP"
pmc -u -d 44 -b 0 "GET TSPROC_FILTER_NP"
pmc -u -d 44 -b 0 "GET SERVO_SETTINGS_NP"
pmc -u -d 44 -b 0 "GET CLOCK_FREQ_EST_NP"
```

Useful SET commands:

```bash
pmc -u -d 44 -b 0 "SET PI_CONSTANTS_NP 0.7 0.3 1.0"
pmc -u -d 44 -b 0 "SET TSPROC_FILTER_NP 1 10"
pmc -u -d 44 -b 0 "SET SERVO_SETTINGS_NP 5 100000"
pmc -u -d 44 -b 0 "SET CLOCK_FREQ_EST_NP 2"
```

Important:

- `PI_CONSTANTS_NP` is valid only with `clock_servo pi`.
- `TSPROC_FILTER_NP` accepts valid moving-average or moving-median filter types.
- `CLOCK_FREQ_EST_NP` uses linuxptp log interval semantics, not raw sample count.
- In `auto` mode, GET values may change again when the adaptive engine applies a
  profile.

## 7. Lab Demo Flow with Frequency Impairment Tool

Use this sequence for a clean lab demonstration:

1. Start with a stable GM and `adap_tuning_mode auto`.
2. Confirm initial ADAP startup and GM selection logs.
3. Confirm current values using `pmc GET`.
4. Apply low impairment and observe stable RMS/max/frequency behavior.
5. Increase jitter or loss using the impairment tool.
6. Watch for mode change logs:

```text
ADAP: switching mode 1 -> 0 (jitter=623.8 loss=0 stable=0)
ADAP: applied params: numOff=8 offThr=200000 kp=0.500 ki=0.200 fltLen=16 freqEst=3 stepThr=20000 maxFreq=900000000
```

7. Remove impairment and observe return toward balanced/aggressive behavior.
8. Switch GM if the lab setup supports multiple GMs and confirm per-GM profile
   selection.

For demos focused on manual parameter comparison, use fixed mode or disable
adaptive tuning so that manual `pmc SET` values remain active.

## 8. How This Helps Deployment

The adaptive layer helps deployment in five practical ways:

- Faster commissioning: field teams can start from known profile defaults instead
  of hand-tuning PI and filter values for every site.
- Better multi-GM behavior: each known GM can have a specific profile, avoiding a
  one-size-fits-all configuration across different timing paths.
- Reduced operational risk: noisy paths move toward conservative tuning instead
  of staying on aggressive lab values.
- Faster lab diagnosis: impairment testing can show visible mode changes and
  applied parameters without restarting ptp4l.
- Safer runtime changes: invalid management values are rejected, PI changes are
  guarded for PI servo only, and filter updates preserve valid delay state.

## 9. Recommended Field Rollout

Use staged rollout rather than enabling full auto everywhere on day one.

Phase 1, baseline:

```ini
adap_tuning_enabled 0
```

Collect normal ptp4l logs, RMS/max/frequency, selected GM, and delay behavior.

Phase 2, fixed conservative:

```ini
adap_tuning_enabled 1
adap_tuning_mode conservative
```

Verify the node remains stable across GM switch, link disturbance, and service
restart.

Phase 3, per-GM profile:

```ini
adap_tuning_enabled 1
adap_tuning_mode balanced
adap_gm_profile_file /etc/linuxptp/adap-gm-profiles.csv
```

Add known GMs one by one, using values proven in lab or soak testing.

Phase 4, controlled auto:

```ini
adap_tuning_enabled 1
adap_tuning_mode auto
adap_eval_interval 1.0
adap_sample_window 10
```

Use this only after field logs prove that automatic profile switching improves
behavior without causing repeated SLAVE/UNCALIBRATED transitions.

## 10. What to Check Before Declaring Ready

Before field release, confirm:

- Cross build passes with `make CROSS_COMPILE=aarch64-linux-gnu-`.
- ptp4l starts and logs `ADAP: created adaptive engine`.
- No repeated `freq_est_interval is too long` messages.
- No repeated `SLAVE to UNCALIBRATED on SYNCHRONIZATION_FAULT` immediately after
  ADAP parameter application.
- `pmc GET PI_CONSTANTS_NP`, `GET TSPROC_FILTER_NP`,
  `GET SERVO_SETTINGS_NP`, and `GET CLOCK_FREQ_EST_NP` return expected values.
- Invalid `pmc SET` values are rejected.
- GM switch applies either the matching GM profile or the configured fallback
  profile.
- Lab impairment test shows stable recovery after impairment is removed.

## 11. Troubleshooting

| Symptom | Likely cause | Action |
|---------|--------------|--------|
| Manual `pmc SET` value changes back | `adap_tuning_mode auto` reapplied a profile | Use fixed mode or disable adaptive tuning for manual tests |
| `PI_CONSTANTS_NP` rejected | Servo is not PI | Set `clock_servo pi` |
| No ADAP logs | Adaptive engine disabled or config not loaded | Check `adap_tuning_enabled` and ptp4l config path |
| Wrong GM profile applied | GM identity string mismatch | Copy the identity exactly from ptp4l logs |
| No GM-specific profile applied | Missing or unreadable profile file | Check `adap_gm_profile_file` path and file permissions |
| Repeated mode switching | Auto thresholds reacting to unstable path | Increase `adap_sample_window`, increase `adap_eval_interval`, or use fixed conservative mode |
| Lab GET differs from SET | Auto mode or GM change overwrote value | Repeat in fixed mode or disabled mode |

## 12. Quick Operator Commands

Start ptp4l:

```bash
./ptp4l -i fm1-mac9 -m -s -f /root/ptp/G.8275.2.cfg
```

Check current adaptive-applied values:

```bash
pmc -u -d 44 -b 0 "GET PI_CONSTANTS_NP"
pmc -u -d 44 -b 0 "GET TSPROC_FILTER_NP"
pmc -u -d 44 -b 0 "GET SERVO_SETTINGS_NP"
pmc -u -d 44 -b 0 "GET CLOCK_FREQ_EST_NP"
```

Run target-board adaptive test:

```bash
./test_adap_tuning.sh 44
```

Watch ADAP logs:

```bash
journalctl -u ptp4l -f | grep "ADAP:"
```


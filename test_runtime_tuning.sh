#!/bin/bash
#
# Adaptive Runtime Tuning Test Script for linuxptp
# Tests the PMC management interface for servo/PI/tsproc/clock parameters
#
# Usage: ./test_runtime_tuning.sh [pmc_port]
# Example: ./test_runtime_tuning.sh 0
#

set -e

PMC_PORT=${1:-0}
PMC_DELAY=1

echo "=========================================="
echo "Adaptive Runtime Tuning Test Suite"
echo "=========================================="
echo "Using PMC port: $PMC_PORT"
echo ""

# Helper to print test section
test_section() {
    echo ""
    echo ">>> $1"
}

# Helper to run PMC command and show output
pmc_get() {
    local tlv=$1
    echo "  [GET] $tlv:"
    pmc -d $PMC_PORT get $tlv 2>/dev/null || echo "    (Failed to GET $tlv)"
    sleep $PMC_DELAY
}

pmc_set() {
    local tlv=$1
    shift
    local params=$@
    echo "  [SET] $tlv $params"
    pmc -d $PMC_PORT set $tlv $params 2>/dev/null || echo "    (Failed to SET $tlv)"
    sleep $PMC_DELAY
}

pmc_verify() {
    local tlv=$1
    echo "  [VERIFY] $tlv after change:"
    pmc -d $PMC_PORT get $tlv 2>/dev/null || echo "    (Failed to GET $tlv)"
    sleep $PMC_DELAY
}

# ============ Test 1: Servo Settings ============
test_section "Test 1: SERVO_SETTINGS_NP (numOffsetValues, offsetThreshold)"

pmc_get "SERVO_SETTINGS_NP"
echo "  Changing servo parameters..."
pmc_set "SERVO_SETTINGS_NP" "numOffsetValues 8 offsetThreshold 150000"
pmc_verify "SERVO_SETTINGS_NP"

# ============ Test 2: PI Constants ============
test_section "Test 2: PI_CONSTANTS_NP (kp, ki, interval)"

pmc_get "PI_CONSTANTS_NP"
echo "  Changing PI constants..."
pmc_set "PI_CONSTANTS_NP" "kp 0.75 ki 0.35 interval 1.2"
pmc_verify "PI_CONSTANTS_NP"

# ============ Test 3: Tsproc Filter ============
test_section "Test 3: TSPROC_FILTER_NP (filter_type, filter_length)"

pmc_get "TSPROC_FILTER_NP"
echo "  Changing tsproc filter length..."
pmc_set "TSPROC_FILTER_NP" "filter_type 0 filter_length 10"
pmc_verify "TSPROC_FILTER_NP"

# ============ Test 4: Clock Freq Est ============
test_section "Test 4: CLOCK_FREQ_EST_NP (freq_est_interval)"

pmc_get "CLOCK_FREQ_EST_NP"
echo "  Changing frequency estimation interval..."
pmc_set "CLOCK_FREQ_EST_NP" "freq_est_interval 512"
pmc_verify "CLOCK_FREQ_EST_NP"

# ============ Test 5: Port Corrections (existing) ============
test_section "Test 5: PORT_CORRECTIONS_NP (existing TLV - verify still works)"

pmc_get "PORT_CORRECTIONS_NP"
echo "  Modifying port corrections (delayAsymmetry)..."
pmc_set "PORT_CORRECTIONS_NP" "egressLatency 0 ingressLatency 0 delayAsymmetry 50000"
pmc_verify "PORT_CORRECTIONS_NP"

# ============ Test 6: Stress Test - Rapid Changes ============
test_section "Test 6: Rapid parameter changes (stress test)"

echo "  Performing 5 rapid servo setting changes..."
for i in {1..5}; do
    val=$((5 + i))
    pmc_set "SERVO_SETTINGS_NP" "numOffsetValues $val offsetThreshold $((100000 + i * 10000))"
done
pmc_verify "SERVO_SETTINGS_NP"

# ============ Test 7: Default Values Reset ============
test_section "Test 7: Reset to conservative values"

echo "  Resetting servo to defaults..."
pmc_set "SERVO_SETTINGS_NP" "numOffsetValues 5 offsetThreshold 100000"
pmc_verify "SERVO_SETTINGS_NP"

echo "  Resetting PI constants to defaults..."
pmc_set "PI_CONSTANTS_NP" "kp 0.7 ki 0.3 interval 1.0"
pmc_verify "PI_CONSTANTS_NP"

# ============ Summary ============
test_section "Summary"
echo "All tests completed. Check logs above for:"
echo "  - No 'failed to send' or 'implicit declaration' errors"
echo "  - GET responses show updated values after SET"
echo "  - ptp4l continued running without restart"
echo ""
echo "To verify actual synchronization improvement:"
echo "  - Monitor ptp4l offset stats"
echo "  - Compare lock-time and offset variance before/after tuning"
echo ""
echo "=========================================="

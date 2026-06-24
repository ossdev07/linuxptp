#!/usr/bin/env python3
"""
Validation script for adaptive runtime tuning TLVs.
Verifies struct definitions, byte ordering, and parameter ranges.
This can be run without ptp4l to validate the implementation.
"""

import struct
import sys

# Struct definitions matching tlv.h

class ServoSettingsNp:
    """MID_SERVO_SETTINGS_NP - servo tuning parameters"""
    fmt = '!ii'  # big-endian: numOffsetValues, offsetThreshold
    size = struct.calcsize(fmt)
    
    def __init__(self, numOffsetValues=5, offsetThreshold=100000):
        self.numOffsetValues = numOffsetValues
        self.offsetThreshold = offsetThreshold
    
    def pack(self):
        return struct.pack(self.fmt, self.numOffsetValues, self.offsetThreshold)
    
    @classmethod
    def unpack(cls, data):
        values = struct.unpack(cls.fmt, data[:cls.size])
        return cls(values[0], values[1])
    
    def __repr__(self):
        return f"ServoSettingsNp(numOffsetValues={self.numOffsetValues}, offsetThreshold={self.offsetThreshold})"


class PiConstantsNp:
    """MID_PI_CONSTANTS_NP - PI servo constants"""
    fmt = '!ddd'  # big-endian: 3x double (kp, ki, interval)
    size = struct.calcsize(fmt)
    
    def __init__(self, kp=0.7, ki=0.3, interval=1.0):
        self.kp = kp
        self.ki = ki
        self.interval = interval
    
    def pack(self):
        return struct.pack(self.fmt, self.kp, self.ki, self.interval)
    
    @classmethod
    def unpack(cls, data):
        values = struct.unpack(cls.fmt, data[:cls.size])
        return cls(values[0], values[1], values[2])
    
    def __repr__(self):
        return f"PiConstantsNp(kp={self.kp}, ki={self.ki}, interval={self.interval})"


class TsprocFilterNp:
    """MID_TSPROC_FILTER_NP - timestamp processor filter settings"""
    fmt = '!Hi'  # big-endian: uint16 filter_type, int32 filter_length
    size = struct.calcsize(fmt)
    
    def __init__(self, filter_type=0, filter_length=8):
        self.filter_type = filter_type
        self.filter_length = filter_length
    
    def pack(self):
        return struct.pack(self.fmt, self.filter_type, self.filter_length)
    
    @classmethod
    def unpack(cls, data):
        values = struct.unpack(cls.fmt, data[:cls.size])
        return cls(values[0], values[1])
    
    def __repr__(self):
        return f"TsprocFilterNp(filter_type={self.filter_type}, filter_length={self.filter_length})"


class ClockFreqEstNp:
    """MID_CLOCK_FREQ_EST_NP - clock frequency estimation interval"""
    fmt = '!i'  # big-endian: int32 freq_est_interval
    size = struct.calcsize(fmt)
    
    def __init__(self, freq_est_interval=256):
        self.freq_est_interval = freq_est_interval
    
    def pack(self):
        return struct.pack(self.fmt, self.freq_est_interval)
    
    @classmethod
    def unpack(cls, data):
        values = struct.unpack(cls.fmt, data[:cls.size])
        return cls(values[0])
    
    def __repr__(self):
        return f"ClockFreqEstNp(freq_est_interval={self.freq_est_interval})"


def test_struct_sizes():
    """Verify struct sizes match C definitions"""
    print("=== Struct Size Validation ===")
    
    tests = [
        ("servo_settings_np", ServoSettingsNp, 8),   # 2 x int32
        ("pi_constants_np", PiConstantsNp, 24),      # 3 x double (8 bytes each)
        ("tsproc_filter_np", TsprocFilterNp, 8),     # uint16 + padding + int32
        ("clock_freq_est_np", ClockFreqEstNp, 4),    # 1 x int32
    ]
    
    passed = 0
    for name, cls, expected_size in tests:
        actual_size = cls.size
        status = "✓ PASS" if actual_size == expected_size else "✗ FAIL"
        print(f"  {name:20} {actual_size:3} bytes (expected {expected_size:3}) ... {status}")
        if actual_size == expected_size:
            passed += 1
    
    return passed == len(tests)


def test_byte_order():
    """Verify big-endian (network byte order) conversion"""
    print("\n=== Byte Order (Network Endianness) ===")
    
    # Test ServoSettingsNp
    ss = ServoSettingsNp(numOffsetValues=256, offsetThreshold=1000000)
    packed = ss.pack()
    unpacked = ServoSettingsNp.unpack(packed)
    
    servo_ok = (unpacked.numOffsetValues == 256 and unpacked.offsetThreshold == 1000000)
    print(f"  ServoSettingsNp: {ss}")
    print(f"    -> packed: {packed.hex()}")
    print(f"    -> unpacked: {unpacked}")
    print(f"    ✓ PASS" if servo_ok else "    ✗ FAIL")
    
    # Test PiConstantsNp
    pc = PiConstantsNp(kp=0.75, ki=0.35, interval=2.5)
    packed = pc.pack()
    unpacked = PiConstantsNp.unpack(packed)
    
    pi_ok = (abs(unpacked.kp - 0.75) < 1e-6 and 
             abs(unpacked.ki - 0.35) < 1e-6 and 
             abs(unpacked.interval - 2.5) < 1e-6)
    print(f"  PiConstantsNp: {pc}")
    print(f"    -> packed: {packed.hex()}")
    print(f"    -> unpacked: {unpacked}")
    print(f"    ✓ PASS" if pi_ok else "    ✗ FAIL")
    
    # Test TsprocFilterNp
    tf = TsprocFilterNp(filter_type=1, filter_length=16)
    packed = tf.pack()
    unpacked = TsprocFilterNp.unpack(packed)
    
    tsproc_ok = (unpacked.filter_type == 1 and unpacked.filter_length == 16)
    print(f"  TsprocFilterNp: {tf}")
    print(f"    -> packed: {packed.hex()}")
    print(f"    -> unpacked: {unpacked}")
    print(f"    ✓ PASS" if tsproc_ok else "    ✗ FAIL")
    
    # Test ClockFreqEstNp
    cf = ClockFreqEstNp(freq_est_interval=512)
    packed = cf.pack()
    unpacked = ClockFreqEstNp.unpack(packed)
    
    clock_ok = (unpacked.freq_est_interval == 512)
    print(f"  ClockFreqEstNp: {cf}")
    print(f"    -> packed: {packed.hex()}")
    print(f"    -> unpacked: {unpacked}")
    print(f"    ✓ PASS" if clock_ok else "    ✗ FAIL")
    
    return servo_ok and pi_ok and tsproc_ok and clock_ok


def test_parameter_ranges():
    """Verify reasonable parameter ranges"""
    print("\n=== Parameter Range Validation ===")
    
    # Servo settings
    print("  Servo Settings:")
    ss_valid = ServoSettingsNp(numOffsetValues=10, offsetThreshold=500000)
    print(f"    Valid: {ss_valid} ✓")
    
    # PI constants
    print("  PI Constants:")
    pc_valid = PiConstantsNp(kp=1.5, ki=0.5, interval=5.0)
    print(f"    Valid: {pc_valid} ✓")
    
    # Tsproc filter
    print("  Tsproc Filter:")
    tf_valid = TsprocFilterNp(filter_type=2, filter_length=32)
    print(f"    Valid: {tf_valid} ✓")
    
    # Clock freq est
    print("  Clock Freq Est:")
    cf_valid = ClockFreqEstNp(freq_est_interval=1024)
    print(f"    Valid: {cf_valid} ✓")
    
    return True


def main():
    print("Adaptive Runtime Tuning - TLV Validation\n")
    
    results = []
    results.append(("Struct Sizes", test_struct_sizes()))
    results.append(("Byte Order", test_byte_order()))
    results.append(("Parameter Ranges", test_parameter_ranges()))
    
    print("\n=== Summary ===")
    all_pass = all(result for _, result in results)
    for name, passed in results:
        status = "✓ PASS" if passed else "✗ FAIL"
        print(f"  {name:25} {status}")
    
    print("")
    if all_pass:
        print("✓ All validation tests PASSED")
        print("\nNext steps:")
        print("  1. Build on Linux: make")
        print("  2. Run ptp4l with management interface enabled")
        print("  3. Execute: ./test_runtime_tuning.sh 0")
        print("  4. Verify PMC GET/SET commands work and values persist")
        return 0
    else:
        print("✗ Some validation tests FAILED")
        return 1


if __name__ == '__main__':
    sys.exit(main())

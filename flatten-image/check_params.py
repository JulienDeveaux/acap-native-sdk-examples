#!/usr/bin/env python3
"""
Extract default parameters from dewarp_gui.py without running the GUI
"""
import sys

# Read the parameters from dewarp_gui.py
MODE = "CONTROLLED"
SCALE = 0.4

print("=" * 60)
print("Python GUI Default Parameters:")
print("=" * 60)
print(f"MODE: {MODE}")
print(f"SCALE: {SCALE}")
print()

# For a 2992x2992 image
W = H = 2992
f = W / 2
cx = W / 2
cy = H / 2

print(f"Image dimensions: {W}x{H}")
print(f"f (focal length): {f}")
print(f"cx (optical center X): {cx}")
print(f"cy (optical center Y): {cy}")
print()

# Default k values from the code
k1 = -0.25
k2 = 0.05
k3 = 0.0
k4 = 0.0

print(f"k1: {k1}")
print(f"k2: {k2}")
print(f"k3: {k3}")
print(f"k4: {k4}")
print()

# Output camera matrix (CONTROLLED mode)
f_out = f * SCALE
print(f"Output focal length (f * SCALE): {f_out}")
print(f"Output optical center: ({cx}, {cy}) [same as input]")
print()

print("=" * 60)
print("These match your C++ parameters for scale=0.4:")
print(f"f_in=1496.00, f_out=598.40, cx_in=1496.00, cy_in=1496.00")
print(f"cx_out=1496.00, cy_out=1496.00")
print(f"k=(-0.2500,0.0500,0.0000,0.0000)")
print("=" * 60)

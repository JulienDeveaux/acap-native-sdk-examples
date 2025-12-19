#!/usr/bin/env python3
"""
Test OpenCV fisheye algorithm directly to verify our C++ implementation
"""
import cv2
import numpy as np

# Parameters
f_in = 1496.0
f_out = 598.4
cx = cy = 1496.0
k1, k2, k3, k4 = -0.25, 0.05, 0.0, 0.0

# Input camera matrix
K = np.array([
    [f_in, 0.0, cx],
    [0.0, f_in, cy],
    [0.0, 0.0, 1.0]
], dtype=np.float32)

# Output camera matrix
K_out = np.array([
    [f_out, 0.0, cx],
    [0.0, f_out, cy],
    [0.0, 0.0, 1.0]
], dtype=np.float32)

# Distortion coefficients
D = np.array([k1, k2, k3, k4], dtype=np.float32)

# Output size
size = (2992, 2992)

# Generate the undistortion maps using OpenCV
print("Generating undistortion maps with OpenCV fisheye model...")
map_x, map_y = cv2.fisheye.initUndistortRectifyMap(
    K, D,
    np.eye(3, dtype=np.float32),  # No rotation
    K_out,
    size,
    cv2.CV_32FC1
)

print(f"Map shapes: map_x={map_x.shape}, map_y={map_y.shape}")
print()

# Test some specific pixels
test_pixels = [
    (1496, 1496),  # Center
    (1496, 1000),  # Above center (should map to straight horizontal line)
    (2000, 1496),  # Right of center
    (1496, 2000),  # Below center
]

print("Testing specific output pixels -> input pixel mapping:")
print("=" * 70)
for u_out, v_out in test_pixels:
    u_in = map_x[v_out, u_out]
    v_in = map_y[v_out, u_out]
    print(f"Output ({u_out:4}, {v_out:4}) -> Input ({u_in:8.2f}, {v_in:8.2f})")

print()
print("If your C++ implementation produces different mappings for these")
print("pixels, that indicates where the algorithm differs from OpenCV.")

#!/usr/bin/env python3
"""
Simple test to verify fisheye undistortion algorithm
Run: python3 test_fisheye.py
"""
import numpy as np

# Parameters matching your C++ code
f_in = 1496.0
f_out = 598.4  # f_in * 0.4
cx_in = cy_in = 1496.0
cx_out = cy_out = 1496.0
k1, k2, k3, k4 = -0.25, 0.05, 0.0, 0.0

# Test a specific output pixel
u_out, v_out = 1496, 1000  # A pixel that should map to a straight line

# Convert to normalized coordinates
x_out = (u_out - cx_out) / f_out
y_out = (v_out - cy_out) / f_out

print(f"Testing output pixel ({u_out}, {v_out})")
print(f"Normalized output coords: x={x_out:.6f}, y={y_out:.6f}")

# Compute angle
r_out = np.sqrt(x_out**2 + y_out**2)
theta = np.arctan(r_out)

print(f"r_out = {r_out:.6f}")
print(f"theta = {theta:.6f} radians = {np.degrees(theta):.2f} degrees")

# Apply distortion model
theta2 = theta * theta
theta4 = theta2 * theta2
theta6 = theta4 * theta2
theta8 = theta6 * theta2
theta_d = theta * (1.0 + k1*theta2 + k2*theta4 + k3*theta6 + k4*theta8)

print(f"theta_d = {theta_d:.6f} radians = {np.degrees(theta_d):.2f} degrees")

# Convert back to distorted coordinates
scale_d = theta_d / r_out if r_out > 1e-8 else 1.0
x_dist = x_out * scale_d
y_dist = y_out * scale_d

print(f"scale_d = {scale_d:.6f}")
print(f"Distorted normalized coords: x={x_dist:.6f}, y={y_dist:.6f}")

# Convert to input pixel coordinates
u_in = f_in * x_dist + cx_in
v_in = f_in * y_dist + cy_in

print(f"Input pixel coordinates: ({u_in:.2f}, {v_in:.2f})")
print()
print("This algorithm should match OpenCV's cv2.fisheye.initUndistortRectifyMap")

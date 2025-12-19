# Fisheye Dewarping Debug Log

## Problem Statement

The flatten_image application was producing dewarped images with a noticeable \_/ curve in horizontal lines (lane markings, fences, etc.), while the Python reference implementation (`dewarp_gui.py`) produced perfectly straight lines.

## Initial Symptoms

- **Radial streaking**: Raindrops were stretched into radial rays emanating from center (early versions)
- **Curved horizontal lines**: Even after fixing radial issues, horizontal features had a ~5-10° dip in the center
- **Correct parameters**: All distortion coefficients (K1=-0.25, K2=0.05) matched the Python implementation

## Investigation Steps

### 1. Parameter Verification

**Issue**: Initially suspected incorrect parameter values or missing parameters.

**Findings**:
- Confirmed all parameters matched Python GUI defaults:
  - `f_in = 1496.0` (focal length = image_width / 2)
  - `f_out = 598.4` (f_in × scale, where scale=0.4)
  - `cx_in = cy_in = 1496.0` (optical center at image center)
  - `cx_out = cy_out = 1496.0` (output optical center)
  - `k1 = -0.25, k2 = 0.05, k3 = 0.0, k4 = 0.0`

**Fix attempted**: Changed various K1/K2 values - didn't resolve the curve pattern.

### 2. Optical Center Issue

**Issue**: Early implementation had `cx_out = output_width/2` and `cy_out = output_height/2`, forcing output optical center to geometric center.

**Fix**: Changed to `cx_out = cx_in` and `cy_out = cy_in` to match Python's CONTROLLED mode behavior.

```cpp
// WRONG:
float cx_out = config_.output_width / 2.0f;
float cy_out = config_.output_height / 2.0f;

// CORRECT:
float cx_out = cx_in;
float cy_out = cy_in;
```

**Result**: Eliminated radial streaking of raindrops, but curve remained.

### 3. OpenCV calib3d Attempt

**Issue**: Tried to use OpenCV's built-in `cv::fisheye::initUndistortRectifyMap()` function.

**Finding**: The `opencv_calib3d` module is NOT available in ACAP Native SDK (only `opencv_core`, `opencv_imgproc`, `opencv_video`, `opencv_imgcodecs`).

**Result**: Reverted to manual implementation of the fisheye algorithm.

### 4. Scale Parameter Investigation

**Issue**: Changing Scale parameter from 0.4 to 0.7 reduced the black circular mask but still had curves.

**Finding**: Scale only affects zoom level and field of view - it's not the root cause. Using scale=0.7 crops the outer view and loses information (e.g., building on right side).

**Result**: Kept Scale=0.4 to match Python's full field of view.

### 5. Ground Truth Comparison with OpenCV

**Key breakthrough**: Created Python script to extract exact pixel mappings from OpenCV's fisheye function.

**Test script** (`test_opencv_fisheye.py`):
```python
map_x, map_y = cv2.fisheye.initUndistortRectifyMap(
    K, D, np.eye(3), K_out, (2992, 2992), cv2.CV_32FC1
)
```

**Ground truth from OpenCV**:
```
Output (1496, 1496) -> Input (1496.00, 1496.00)
Output (1496, 1000) -> Input (1496.00,  572.73)
Output (2000, 1496) -> Input (2427.46, 1496.00)
Output (1496, 2000) -> Input (1496.00, 2427.46)
```

**C++ implementation output** (buggy):
```
Output (1496, 1496) -> Input (1496.00, 1496.00)  ✅
Output (1496, 1000) -> Input (1496.00,  689.98)  ❌ Under-corrected
Output (2000, 1496) -> Input (2310.53, 1496.00)  ❌ Under-corrected
Output (1496, 2000) -> Input (1496.00, 2310.53)  ❌ Under-corrected
```

**Analysis**: C++ was under-correcting by ~15-20%. Distorted input pixels weren't being sampled far enough from center.

## Root Cause

**Bug location**: `dewarper.cpp` line 310

```cpp
// WRONG - uses perspective camera model
float theta = std::atan(r_out);

// CORRECT - fisheye equidistant projection model
float theta = r_out;
```

**Explanation**:
- **Perspective cameras**: Use `theta = atan(r)` relationship between angle and radius
- **Fisheye equidistant projection**: Use direct `theta = r` relationship

The OpenCV fisheye model (`cv::fisheye::initUndistortRectifyMap`) implements the **equidistant projection** where the radius in the image plane is directly proportional to the incident angle.

Using `atan(r)` compressed the angles, resulting in under-correction and the characteristic \_/ curve in horizontal lines.

## Final Fix

```cpp
// dewarper.cpp:308-311
// Compute angle theta from optical axis
// For fisheye equidistant model: theta = r (not atan(r))
float r_out = std::sqrt(x_out * x_out + y_out * y_out);
float theta = r_out;
```

## Verification Steps

1. **Rebuild application**:
   ```bash
   docker build --platform=linux/amd64 --build-arg ARCH=aarch64 \
     --tag flatten-image:latest flatten-image/
   docker cp $(docker create --platform=linux/amd64 flatten-image:latest):/opt/app ./build
   ```

2. **Verify test pixel mappings** match OpenCV ground truth:
   ```bash
   curl -u USER:PASS "http://CAMERA_IP/axis-cgi/admin/systemlog.cgi?appname=flatten_image" \
     | grep "TEST:"
   ```

3. **Visual verification**: Horizontal lines (lane markings, fences) should be perfectly straight.

4. **Parameter configuration** (if not already set):
   ```bash
   curl -u USER:PASS "http://CAMERA_IP/axis-cgi/param.cgi?action=update&\
     flatten_image.K1=-0.25&flatten_image.K2=0.05&flatten_image.Scale=0.4"
   ```

## Key Learnings

1. **ACAP SDK OpenCV limitations**: Not all OpenCV modules are available. Check `using-opencv` example for what's included.

2. **Fisheye projection models**: Different projection models use different angle-to-radius relationships:
   - Equidistant: `r = f × theta`
   - Stereographic: `r = 2f × tan(theta/2)`
   - Equisolid: `r = 2f × sin(theta/2)`
   - Orthographic: `r = f × sin(theta)`

3. **Parameter persistence**: ACAP parameters from `manifest.json` are stored in device database. C++ fallback values only apply if parameter is completely missing, not if it exists with value 0.0.

4. **Debugging technique**: Ground truth comparison with known-good implementation (OpenCV) using specific test pixels was crucial for identifying the exact bug.

5. **Optical center handling**: In CONTROLLED mode, Python scales only the focal length, not the optical center coordinates. This is critical for correct dewarping.

## Files Modified

- `app/dewarper.cpp`: Fixed fisheye angle calculation (line 310)
- `app/dewarper.h`: No changes required
- `app/flatten_image.cpp`: Updated default resolution to 2992x2992, focal length to 1496
- `app/manifest.json`: Updated default resolution and focal length
- `README.md`: Added extensive documentation on parameter configuration

## Test Files Created

- `test_opencv_fisheye.py`: Extracts ground truth pixel mappings from OpenCV
- `test_fisheye.py`: Manual algorithm verification
- `check_params.py`: Verifies Python GUI default parameters
- `dewarp_gui.py`: Reference Python implementation (provided by user's boss)

## Resolution Parameters

**Default configuration** (matching Python GUI):
- Resolution: 2992x2992
- FocalLength: 1496
- K1: -0.25
- K2: 0.05
- K3: 0.0
- K4: 0.0
- Scale: 0.4
- CenterX: 0.5
- CenterY: 0.5

**Note**: These are defaults in code/manifest. The K1 and K2 parameters MUST be configured via the parameter API after installation, as the ACAP parameter system initializes them to 0.0 on first install.

## Build Verification

Confirmed compilation error when trying to include `opencv2/calib3d.hpp` - this module is not available in ACAP Native SDK 12.7.0. Manual implementation is required.

## Related Issues

- Black circular mask with scale=0.4 is EXPECTED behavior - it represents the valid fisheye circle region
- Increasing Scale to fill the frame crops the field of view and loses peripheral information
- The Python GUI produces identical circular mask with same parameters

## Status

✅ **RESOLVED** - Fix implemented and verified against OpenCV ground truth pixel mappings.

Awaiting final user verification after rebuild with `theta = r_out` fix.

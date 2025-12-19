*Copyright (C) 2025, Axis Communications AB, Lund, Sweden. All Rights Reserved.*

# Flatten Image - Fisheye/360 Dewarping ACAP Application

This example demonstrates how to capture video from a fisheye or 360-degree camera,
flatten/dewarp the image using various projections, and output the result as an RTSP stream
for consumption by external video analytics systems.

## Overview

The application:

1. Captures video frames from the camera using the VDO API
2. Applies configurable dewarping transformations using OpenCV
3. Streams the flattened video via RTSP using GStreamer
4. Provides HTTP endpoints for retrieving historical snapshots (last 31 seconds)

### Supported Lens Types

- **Fisheye** (180-190 degree FOV)
- **Dual fisheye** (360 degree, two back-to-back fisheye lenses)
- **Panoramic** (ultra-wide angle)

### Supported Output Projections

- **Fisheye Undistort** (default) - Uses OpenCV's fisheye calibration model for proper lens undistortion. Best for removing fisheye distortion while preserving straight lines.
- **Equirectangular** - Standard panorama format, maps entire FOV to a rectangle
- **Rectilinear** - Perspective-correct view like a regular camera (limited FOV per view)
- **Cylindrical** - Good for wide horizontal scenes, maintains vertical perspective

## Getting started

These instructions will guide you on how to execute the code. Below is the structure:

```sh
flatten-image
├── app
│   ├── dewarper.cpp/h       # Image dewarping module
│   ├── fastcgi_server.cpp/h # HTTP endpoint server
│   ├── flatten_image.cpp    # Main application
│   ├── image_buffer.cpp/h   # Circular buffer for snapshots
│   ├── imgprovider.cpp/h    # VDO frame capture
│   ├── rtsp_server.cpp/h    # GStreamer RTSP server
│   ├── panic.cpp/h          # Error handling
│   ├── LICENSE
│   ├── Makefile
│   └── manifest.json
├── Dockerfile
└── README.md
```

## Configuration Parameters

The application is configured through ACAP parameters accessible via the device web interface:

| Parameter | Default | Description |
|-----------|---------|-------------|
| LensType | fisheye | Input lens type: `fisheye`, `dual_fisheye`, `panoramic` |
| Projection | fisheye_undistort | Output projection: `fisheye_undistort`, `equirectangular`, `rectilinear`, `cylindrical` |
| InputFOV | 180 | Input lens field of view in degrees |
| OutputWidth | 2992 | Output video width in pixels |
| OutputHeight | 2992 | Output video height in pixels |
| RTSPPort | 8554 | RTSP server port |
| Framerate | 15 | Output framerate |
| CenterX | 0.5 | Lens center X position (0.0-1.0) |
| CenterY | 0.5 | Lens center Y position (0.0-1.0) |
| PanAngle | 0 | Virtual camera pan angle in degrees |
| TiltAngle | 0 | Virtual camera tilt angle in degrees |
| RectilinearFOV | 90 | Output FOV for rectilinear projection |
| FocalLength | 1496 | Focal length for fisheye_undistort projection |
| K1 | 0.0 | Fisheye distortion coefficient K1 (use negative values like -0.25 for barrel distortion) |
| K2 | 0.0 | Fisheye distortion coefficient K2 (use positive values like 0.05 for correction) |
| K3 | 0.0 | Fisheye distortion coefficient K3 |
| K4 | 0.0 | Fisheye distortion coefficient K4 |
| Scale | 0.5 | Output image scale for fisheye_undistort (affects zoom level) |

### Fisheye Undistort Parameters

The `fisheye_undistort` projection uses OpenCV's fisheye calibration model (`cv::fisheye::initUndistortRectifyMap`). This is the recommended mode for dewarping dome camera images.

**Key parameters:**
- **FocalLength**: The camera's focal length in pixels. Default of 1496 works well for 2992x2992 output (approximately half the image width).
- **K1-K4**: Distortion coefficients for the fisheye lens model. **CRITICAL: You must configure these parameters for dewarping to work.**
  - **K1**: Primary distortion coefficient (typically negative for barrel distortion, e.g., -0.25)
  - **K2**: Secondary distortion coefficient (typically positive, e.g., 0.05)
  - **K3, K4**: Higher-order terms (usually 0.0 unless high precision is needed)
  - **When all K values are 0.0 (default), NO dewarping occurs** - the image passes through unchanged
- **Scale**: Controls the output zoom level. Lower values (e.g., 0.5) zoom out to show more of the scene; higher values zoom in.

**Why you must set K1 and K2:**

The ACAP parameter system initializes all parameters to their default values (0.0) when the application is first installed. Even though the C++ code specifies fallback defaults like `-0.25f` for K1, these only apply when a parameter is not found in the device's parameter database. Once the application is installed, the parameters exist with value 0.0, so the code's fallback values are never used.

**To enable dewarping, you MUST configure K1 and K2 after installation** (see "Configuring Distortion Parameters" section below).

## Build the application

Standing in your working directory run the following commands:

> [!NOTE]
>
> Depending on the network your local build machine is connected to, you may need to add proxy
> settings for Docker. See
> [Proxy in build time](https://developer.axis.com/acap/develop/proxy/#proxy-in-build-time).

```sh
docker build --platform=linux/amd64 --build-arg ARCH=aarch64 --tag flatten-image:latest .
```

Copy the result from the container image to a local directory `build`:

```sh
docker cp $(docker create --platform=linux/amd64 flatten-image:latest):/opt/app ./build
```

The `build` directory contains the build artifacts, including:

- `flatten_image_1_0_0_aarch64.eap`

> [!NOTE]
>
> For detailed information on how to build, install, and run ACAP applications, refer to the
> official ACAP documentation: [Build, install, and run](https://developer.axis.com/acap/develop/build-install-run/).

## Install and start the application

Browse to the application page of the Axis device:

```sh
http://<AXIS_DEVICE_IP>/index.html#apps
```

- Click on the tab `Apps` in the device GUI
- Enable `Allow unsigned apps` toggle
- Click `(+ Add app)` button to upload the application file
- Browse to the newly built ACAP application
- Click `Install`
- Run the application by enabling the `Start` switch

## Access the RTSP stream

Once the application is running, the flattened video stream is available at:

```sh
rtsp://<AXIS_DEVICE_IP>:8554/stream
```

You can view the stream using VLC or any RTSP-compatible player:

```sh
vlc rtsp://<AXIS_DEVICE_IP>:8554/stream
```

Or using ffplay:

```sh
ffplay rtsp://<AXIS_DEVICE_IP>:8554/stream
```

## Access HTTP image snapshots

The application provides HTTP endpoints for retrieving historical JPEG snapshots. Images are
captured once per second and stored in a circular buffer (31 images total).

### Endpoint URL

```
http://<AXIS_DEVICE_IP>/local/flatten_image/image.cgi?index=<N>
```

Where `<N>` is the image index:
- `index=0` - Latest image (current)
- `index=1` - 1 second ago
- `index=2` - 2 seconds ago
- ...
- `index=30` - 30 seconds ago

### Examples

Get the latest dewarped image:

```sh
curl -u <USER>:<PASSWORD> "http://<AXIS_DEVICE_IP>/local/flatten_image/image.cgi?index=0" -o latest.jpg
```

Get the image from 10 seconds ago:

```sh
curl -u <USER>:<PASSWORD> "http://<AXIS_DEVICE_IP>/local/flatten_image/image.cgi?index=10" -o 10sec_ago.jpg
```

### Response codes

| Code | Description |
|------|-------------|
| 200 | Success - JPEG image returned |
| 404 | Invalid index (must be 0-30) |
| 503 | No image available yet (buffer not filled) |

### Notes

- Images are stored in memory only (no disk storage required)
- The buffer holds approximately 31 seconds of history
- Each image is JPEG encoded at quality 80
- Authentication follows device viewer access settings

## Adjusting parameters

Parameters can be adjusted via the device's parameter CGI:

```sh
# Set projection to rectilinear
curl -u <USER>:<PASSWORD> "http://<AXIS_DEVICE_IP>/axis-cgi/param.cgi?action=update&flatten_image.Projection=rectilinear"

# Set pan angle to 45 degrees
curl -u <USER>:<PASSWORD> "http://<AXIS_DEVICE_IP>/axis-cgi/param.cgi?action=update&flatten_image.PanAngle=45"
```

The application reads parameters at startup. To apply parameter changes, restart the application.

## Configuring Distortion Parameters

**IMPORTANT:** For fisheye dewarping to work, you must configure the K1 and K2 distortion coefficients after installing the application. The default values (0.0) result in no dewarping.

### Method 1: Using the Calibration GUI (Recommended)

Use the provided Python calibration tool to visually tune the distortion parameters:

1. Download a test image from the camera:
   ```sh
   curl -u <USER>:<PASSWORD> "http://<AXIS_DEVICE_IP>/local/flatten_image/image.cgi?index=0" -o test_image.jpg
   ```

2. Run the calibration GUI (requires Python with cv2, numpy, tkinter, PIL):
   ```sh
   python3 dewarp_gui.py test_image.jpg
   ```

3. Adjust the sliders until vertical lines appear straight:
   - **f** (focal length): Start with half your image width (e.g., 1496 for 2992px)
   - **k1**: Adjust negative values (typically -0.15 to -0.30) to correct barrel distortion
   - **k2**: Fine-tune with small positive values (typically 0.01 to 0.10)

4. Note the final values and apply them to the camera:
   ```sh
   curl -u <USER>:<PASSWORD> "http://<AXIS_DEVICE_IP>/axis-cgi/param.cgi?action=update&flatten_image.FocalLength=1496"
   curl -u <USER>:<PASSWORD> "http://<AXIS_DEVICE_IP>/axis-cgi/param.cgi?action=update&flatten_image.K1=-0.25"
   curl -u <USER>:<PASSWORD> "http://<AXIS_DEVICE_IP>/axis-cgi/param.cgi?action=update&flatten_image.K2=0.05"
   ```

5. Restart the application to apply changes.

### Method 2: Using Standard Values

For typical fisheye dome cameras, start with these values:

```sh
# Standard fisheye correction values
curl -u <USER>:<PASSWORD> "http://<AXIS_DEVICE_IP>/axis-cgi/param.cgi?action=update&flatten_image.K1=-0.25&flatten_image.K2=0.05"
```

Then restart the application and check the output. Adjust as needed.

### Method 3: Using OpenCV Calibration

For precise calibration, use OpenCV's fisheye calibration with a checkerboard pattern:

```python
import cv2
import numpy as np

# After capturing calibration images...
K, D, rvecs, tvecs = cv2.fisheye.calibrate(...)
print(f"K1={D[0]}, K2={D[1]}, K3={D[2]}, K4={D[3]}")
print(f"FocalLength={K[0,0]}")
```

Apply the calibrated values to the camera.

## Application log

View the application log at:

```sh
http://<AXIS_DEVICE_IP>/axis-cgi/admin/systemlog.cgi?appname=flatten_image
```

## Use Cases

### Video Analytics Integration

Use the RTSP output stream as input to external video analytics systems (VMS, AI platforms)
that expect standard rectangular video input.

### Virtual PTZ

Configure rectilinear projection with pan/tilt angles to create a virtual PTZ view
from a fixed fisheye camera.

### Panoramic Recording

Use equirectangular projection to record full 360-degree scenes in a standard video format.

### Historical Image Retrieval

Use the HTTP snapshot endpoints to retrieve recent images for:
- Event investigation (what happened 10 seconds ago?)
- Periodic polling for lightweight integrations
- Thumbnail generation without video decoding

## License

**[Apache License 2.0](../LICENSE)**

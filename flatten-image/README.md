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

### Supported Lens Types

- **Fisheye** (180-190 degree FOV)
- **Dual fisheye** (360 degree, two back-to-back fisheye lenses)
- **Panoramic** (ultra-wide angle)

### Supported Output Projections

- **Equirectangular** - Standard panorama format, maps entire FOV to a rectangle
- **Rectilinear** - Perspective-correct view like a regular camera (limited FOV per view)
- **Cylindrical** - Good for wide horizontal scenes, maintains vertical perspective

## Getting started

These instructions will guide you on how to execute the code. Below is the structure:

```sh
flatten-image
├── app
│   ├── dewarper.cpp/h       # Image dewarping module
│   ├── flatten_image.cpp    # Main application
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
| Projection | equirectangular | Output projection: `equirectangular`, `rectilinear`, `cylindrical` |
| InputFOV | 180 | Input lens field of view in degrees |
| OutputWidth | 1920 | Output video width in pixels |
| OutputHeight | 1080 | Output video height in pixels |
| RTSPPort | 8554 | RTSP server port |
| Framerate | 15 | Output framerate |
| CenterX | 0.5 | Lens center X position (0.0-1.0) |
| CenterY | 0.5 | Lens center Y position (0.0-1.0) |
| PanAngle | 0 | Virtual camera pan angle in degrees |
| TiltAngle | 0 | Virtual camera tilt angle in degrees |
| RectilinearFOV | 90 | Output FOV for rectilinear projection |

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

## Adjusting parameters

Parameters can be adjusted via the device's parameter CGI:

```sh
# Set projection to rectilinear
curl -u <USER>:<PASSWORD> "http://<AXIS_DEVICE_IP>/axis-cgi/param.cgi?action=update&flatten_image.Projection=rectilinear"

# Set pan angle to 45 degrees
curl -u <USER>:<PASSWORD> "http://<AXIS_DEVICE_IP>/axis-cgi/param.cgi?action=update&flatten_image.PanAngle=45"
```

The application reads parameters at startup. To apply parameter changes, restart the application.

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

## License

**[Apache License 2.0](../LICENSE)**

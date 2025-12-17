/**
 * Copyright (C) 2025, Axis Communications AB, Lund, Sweden
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <axparameter.h>
#include <glib.h>
#include <signal.h>
#include <stdlib.h>
#include <syslog.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#pragma GCC diagnostic pop

#include "dewarper.h"
#include "imgprovider.h"
#include "panic.h"
#include "rtsp_server.h"

#define APP_NAME "flatten_image"

static volatile sig_atomic_t shutdown_requested = 0;

static void handle_sigterm(int signum) {
    (void)signum;
    shutdown_requested = 1;
}

static void handle_sigint(int signum) {
    (void)signum;
    shutdown_requested = 1;
}

// Read a string parameter from axparameter
static std::string get_param_string(AXParameter* handle, const char* name, const char* default_val) {
    g_autoptr(GError) error = nullptr;
    gchar* value            = nullptr;

    if (!ax_parameter_get(handle, name, &value, &error)) {
        syslog(LOG_WARNING, "Failed to get parameter %s: %s", name, error->message);
        return default_val;
    }

    std::string result(value);
    g_free(value);
    return result;
}

// Read a float parameter
static float get_param_float(AXParameter* handle, const char* name, float default_val) {
    std::string str = get_param_string(handle, name, "");
    if (str.empty()) {
        return default_val;
    }
    return std::stof(str);
}

// Read an unsigned int parameter
static unsigned int get_param_uint(AXParameter* handle, const char* name, unsigned int default_val) {
    std::string str = get_param_string(handle, name, "");
    if (str.empty()) {
        return default_val;
    }
    return static_cast<unsigned int>(std::stoul(str));
}

int main(void) {
    openlog(APP_NAME, LOG_PID, LOG_USER);
    syslog(LOG_INFO, "Starting flatten_image application");

    // Setup signal handlers
    if (signal(SIGTERM, handle_sigterm) == SIG_ERR) {
        panic("Failed to install SIGTERM handler");
    }
    if (signal(SIGINT, handle_sigint) == SIG_ERR) {
        panic("Failed to install SIGINT handler");
    }

    // Initialize parameter handling
    g_autoptr(GError) param_error = nullptr;
    AXParameter* param_handle     = ax_parameter_new(APP_NAME, &param_error);
    if (!param_handle) {
        panic("Failed to create parameter handle: %s", param_error->message);
    }

    // Read configuration from parameters
    std::string lens_type_str   = get_param_string(param_handle, "LensType", "fisheye");
    std::string projection_str  = get_param_string(param_handle, "Projection", "equirectangular");
    float input_fov             = get_param_float(param_handle, "InputFOV", 180.0f);
    unsigned int output_width   = get_param_uint(param_handle, "OutputWidth", 1920);
    unsigned int output_height  = get_param_uint(param_handle, "OutputHeight", 1080);
    unsigned int rtsp_port      = get_param_uint(param_handle, "RTSPPort", 8554);
    unsigned int framerate      = get_param_uint(param_handle, "Framerate", 15);
    float center_x              = get_param_float(param_handle, "CenterX", 0.5f);
    float center_y              = get_param_float(param_handle, "CenterY", 0.5f);
    float pan_angle             = get_param_float(param_handle, "PanAngle", 0.0f);
    float tilt_angle            = get_param_float(param_handle, "TiltAngle", 0.0f);
    float rectilinear_fov       = get_param_float(param_handle, "RectilinearFOV", 90.0f);

    syslog(LOG_INFO,
           "Configuration: lens=%s, projection=%s, output=%ux%u, fps=%u",
           lens_type_str.c_str(),
           projection_str.c_str(),
           output_width,
           output_height,
           framerate);

    // Setup VDO stream for input
    // Use same resolution as output for simplicity, could be different
    VdoFormat vdo_format       = VDO_FORMAT_YUV;
    double vdo_framerate       = static_cast<double>(framerate);
    unsigned int stream_width  = 0;
    unsigned int stream_height = 0;

    // Try to get a suitable input resolution
    if (!choose_stream_resolution(output_width,
                                  output_height,
                                  vdo_format,
                                  nullptr,
                                  "all",
                                  &stream_width,
                                  &stream_height)) {
        panic("Failed to choose stream resolution");
    }

    syslog(LOG_INFO, "Creating VDO stream %ux%u at %.1f fps", stream_width, stream_height, vdo_framerate);

    img_provider_t* img_provider =
        create_img_provider(stream_width, stream_height, 3, vdo_format, vdo_framerate);
    if (!img_provider) {
        panic("Failed to create image provider");
    }

    // Configure dewarper
    DewarperConfig dewarp_config;
    dewarp_config.lens_type       = Dewarper::parse_lens_type(lens_type_str);
    dewarp_config.projection      = Dewarper::parse_projection_type(projection_str);
    dewarp_config.input_fov       = input_fov;
    dewarp_config.input_width     = img_provider->width;
    dewarp_config.input_height    = img_provider->height;
    dewarp_config.output_width    = output_width;
    dewarp_config.output_height   = output_height;
    dewarp_config.center_x        = center_x;
    dewarp_config.center_y        = center_y;
    dewarp_config.pan_angle       = pan_angle;
    dewarp_config.tilt_angle      = tilt_angle;
    dewarp_config.rectilinear_fov = rectilinear_fov;

    Dewarper dewarper;
    if (!dewarper.init(dewarp_config)) {
        panic("Failed to initialize dewarper");
    }

    // Configure RTSP server
    RTSPServerConfig rtsp_config;
    rtsp_config.port        = rtsp_port;
    rtsp_config.width       = output_width;
    rtsp_config.height      = output_height;
    rtsp_config.fps         = framerate;
    rtsp_config.mount_point = "/stream";

    RTSPServer rtsp_server;
    if (!rtsp_server.init(rtsp_config)) {
        panic("Failed to initialize RTSP server");
    }

    if (!rtsp_server.start()) {
        panic("Failed to start RTSP server");
    }

    // Start video stream
    syslog(LOG_INFO, "Starting video capture");
    if (!img_provider_start(img_provider)) {
        panic("Failed to start image provider");
    }

    // Create OpenCV mats for processing
    // NV12 format: height * 1.5 for Y plane + UV plane
    cv::Mat nv12_mat(img_provider->height * 3 / 2, img_provider->width, CV_8UC1);
    cv::Mat output_bgr(output_height, output_width, CV_8UC3);

    syslog(LOG_INFO,
           "Entering main loop - RTSP stream available at rtsp://DEVICE_IP:%u/stream",
           rtsp_port);

    // Main processing loop
    while (!shutdown_requested) {
        g_autoptr(GError) vdo_error = nullptr;

        // Get frame from VDO
        g_autoptr(VdoBuffer) vdo_buf = img_provider_get_frame(img_provider);
        if (!vdo_buf) {
            syslog(LOG_WARNING, "No buffer received, possible global rotation");
            continue;
        }

        // Map VDO buffer to OpenCV Mat
        nv12_mat.data = static_cast<uint8_t*>(vdo_buffer_get_data(vdo_buf));

        // Dewarp the frame
        if (!dewarper.process(nv12_mat, output_bgr)) {
            syslog(LOG_WARNING, "Dewarping failed");
            goto unref_buffer;
        }

        // Push frame to RTSP server
        rtsp_server.push_frame(output_bgr);

    unref_buffer:
        // Release VDO buffer
        if (!vdo_stream_buffer_unref(img_provider->vdo_stream, &vdo_buf, &vdo_error)) {
            if (!vdo_error_is_expected(&vdo_error)) {
                syslog(LOG_ERR, "Unexpected VDO error: %s", vdo_error->message);
            }
            g_clear_error(&vdo_error);
        }
    }

    syslog(LOG_INFO, "Shutting down");

    // Cleanup
    rtsp_server.stop();
    destroy_img_provider(img_provider);
    ax_parameter_free(param_handle);

    syslog(LOG_INFO, "Application exited cleanly");
    closelog();

    return EXIT_SUCCESS;
}

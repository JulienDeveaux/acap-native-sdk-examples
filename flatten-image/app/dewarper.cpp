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

#include "dewarper.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
#include <opencv2/imgproc.hpp>
#pragma GCC diagnostic pop

#include <cmath>
#include <syslog.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

Dewarper::Dewarper() {}

Dewarper::~Dewarper() {}

bool Dewarper::init(const DewarperConfig& config) {
    config_ = config;
    build_lookup_tables();
    initialized_ = true;
    syslog(LOG_INFO,
           "Dewarper initialized: %ux%u -> %ux%u, projection=%d",
           config_.input_width,
           config_.input_height,
           config_.output_width,
           config_.output_height,
           static_cast<int>(config_.projection));
    return true;
}

bool Dewarper::update_config(const DewarperConfig& config) {
    config_ = config;
    build_lookup_tables();
    return true;
}

bool Dewarper::process(const cv::Mat& input_nv12, cv::Mat& output_bgr) {
    if (!initialized_) {
        return false;
    }

    // Convert NV12 to BGR
    cv::cvtColor(input_nv12, bgr_temp_, cv::COLOR_YUV2BGR_NV12, 3);

    // Apply the remap transformation
    cv::remap(bgr_temp_, output_bgr, map_x_, map_y_, cv::INTER_LINEAR, cv::BORDER_CONSTANT);

    return true;
}

void Dewarper::build_lookup_tables() {
    switch (config_.projection) {
        case ProjectionType::EQUIRECTANGULAR:
            map_x_.create(config_.output_height, config_.output_width, CV_32FC1);
            map_y_.create(config_.output_height, config_.output_width, CV_32FC1);
            build_equirectangular_map();
            break;
        case ProjectionType::RECTILINEAR:
            map_x_.create(config_.output_height, config_.output_width, CV_32FC1);
            map_y_.create(config_.output_height, config_.output_width, CV_32FC1);
            build_rectilinear_map();
            break;
        case ProjectionType::CYLINDRICAL:
            map_x_.create(config_.output_height, config_.output_width, CV_32FC1);
            map_y_.create(config_.output_height, config_.output_width, CV_32FC1);
            build_cylindrical_map();
            break;
        case ProjectionType::FISHEYE_UNDISTORT:
            build_fisheye_undistort_map();
            break;
    }

    syslog(LOG_INFO, "Built lookup tables for projection type %d", static_cast<int>(config_.projection));
}

void Dewarper::build_equirectangular_map() {
    // Map from equirectangular output to fisheye input
    // Equirectangular: x maps to longitude (-pi to pi), y maps to latitude (-pi/2 to pi/2)

    float cx         = config_.center_x * config_.input_width;
    float cy         = config_.center_y * config_.input_height;
    float fov_rad    = config_.input_fov * static_cast<float>(M_PI) / 180.0f;
    float radius     = std::min(config_.input_width, config_.input_height) / 2.0f;
    float pan_rad    = config_.pan_angle * static_cast<float>(M_PI) / 180.0f;
    float tilt_rad   = config_.tilt_angle * static_cast<float>(M_PI) / 180.0f;

    for (unsigned int y = 0; y < config_.output_height; y++) {
        for (unsigned int x = 0; x < config_.output_width; x++) {
            // Normalize output coordinates to [-1, 1]
            float norm_x = (2.0f * x / config_.output_width) - 1.0f;
            float norm_y = (2.0f * y / config_.output_height) - 1.0f;

            // Convert to spherical coordinates (longitude, latitude)
            float longitude = norm_x * static_cast<float>(M_PI) + pan_rad;
            float latitude  = norm_y * static_cast<float>(M_PI) / 2.0f + tilt_rad;

            // Convert spherical to 3D cartesian
            float cart_x = std::cos(latitude) * std::sin(longitude);
            float cart_y = std::sin(latitude);
            float cart_z = std::cos(latitude) * std::cos(longitude);

            // Project to fisheye (equidistant projection)
            float theta = std::acos(cart_z);
            float phi   = std::atan2(cart_y, cart_x);

            // Map theta to radius in fisheye image
            float r = (theta / (fov_rad / 2.0f)) * radius;

            // Convert polar to cartesian in fisheye image
            float fish_x = cx + r * std::cos(phi);
            float fish_y = cy + r * std::sin(phi);

            // Store in lookup tables
            map_x_.at<float>(y, x) = fish_x;
            map_y_.at<float>(y, x) = fish_y;
        }
    }
}

void Dewarper::build_rectilinear_map() {
    // Map from rectilinear (perspective) output to fisheye input
    // Rectilinear projection removes distortion for a limited FOV

    float cx            = config_.center_x * config_.input_width;
    float cy            = config_.center_y * config_.input_height;
    float fov_rad       = config_.input_fov * static_cast<float>(M_PI) / 180.0f;
    float radius        = std::min(config_.input_width, config_.input_height) / 2.0f;
    float out_fov_rad   = config_.rectilinear_fov * static_cast<float>(M_PI) / 180.0f;
    float pan_rad       = config_.pan_angle * static_cast<float>(M_PI) / 180.0f;
    float tilt_rad      = config_.tilt_angle * static_cast<float>(M_PI) / 180.0f;

    // Focal length for output rectilinear image
    float focal = config_.output_width / (2.0f * std::tan(out_fov_rad / 2.0f));

    for (unsigned int y = 0; y < config_.output_height; y++) {
        for (unsigned int x = 0; x < config_.output_width; x++) {
            // Convert output pixel to normalized camera coordinates
            float norm_x = (x - config_.output_width / 2.0f) / focal;
            float norm_y = (y - config_.output_height / 2.0f) / focal;

            // Create ray direction (pointing forward)
            float ray_x = norm_x;
            float ray_y = norm_y;
            float ray_z = 1.0f;

            // Apply pan rotation (around Y axis)
            float cos_pan  = std::cos(pan_rad);
            float sin_pan  = std::sin(pan_rad);
            float rx       = ray_x * cos_pan + ray_z * sin_pan;
            float rz       = -ray_x * sin_pan + ray_z * cos_pan;
            ray_x          = rx;
            ray_z          = rz;

            // Apply tilt rotation (around X axis)
            float cos_tilt = std::cos(tilt_rad);
            float sin_tilt = std::sin(tilt_rad);
            float ry       = ray_y * cos_tilt - ray_z * sin_tilt;
            rz             = ray_y * sin_tilt + ray_z * cos_tilt;
            ray_y          = ry;
            ray_z          = rz;

            // Normalize ray
            float ray_len = std::sqrt(ray_x * ray_x + ray_y * ray_y + ray_z * ray_z);
            ray_x /= ray_len;
            ray_y /= ray_len;
            ray_z /= ray_len;

            // Convert ray to fisheye coordinates (equidistant projection)
            float theta = std::acos(ray_z);
            float phi   = std::atan2(ray_y, ray_x);

            // Check if point is within fisheye FOV
            if (theta > fov_rad / 2.0f) {
                map_x_.at<float>(y, x) = -1.0f;
                map_y_.at<float>(y, x) = -1.0f;
                continue;
            }

            // Map theta to radius in fisheye image
            float r = (theta / (fov_rad / 2.0f)) * radius;

            // Convert polar to cartesian in fisheye image
            float fish_x = cx + r * std::cos(phi);
            float fish_y = cy + r * std::sin(phi);

            map_x_.at<float>(y, x) = fish_x;
            map_y_.at<float>(y, x) = fish_y;
        }
    }
}

void Dewarper::build_cylindrical_map() {
    // Map from cylindrical output to fisheye input
    // Cylindrical projection: horizontal is perspective, vertical is equidistant

    float cx         = config_.center_x * config_.input_width;
    float cy         = config_.center_y * config_.input_height;
    float fov_rad    = config_.input_fov * static_cast<float>(M_PI) / 180.0f;
    float radius     = std::min(config_.input_width, config_.input_height) / 2.0f;
    float pan_rad    = config_.pan_angle * static_cast<float>(M_PI) / 180.0f;
    float tilt_rad   = config_.tilt_angle * static_cast<float>(M_PI) / 180.0f;

    for (unsigned int y = 0; y < config_.output_height; y++) {
        for (unsigned int x = 0; x < config_.output_width; x++) {
            // Normalize coordinates
            float norm_x = (2.0f * x / config_.output_width) - 1.0f;
            float norm_y = (2.0f * y / config_.output_height) - 1.0f;

            // Cylindrical: horizontal angle wraps around, vertical is linear
            float longitude = norm_x * static_cast<float>(M_PI) + pan_rad;
            float vertical  = norm_y * (fov_rad / 2.0f) + tilt_rad;

            // Create ray on cylinder surface
            float ray_x = std::sin(longitude);
            float ray_y = std::tan(vertical);
            float ray_z = std::cos(longitude);

            // Normalize ray
            float ray_len = std::sqrt(ray_x * ray_x + ray_y * ray_y + ray_z * ray_z);
            ray_x /= ray_len;
            ray_y /= ray_len;
            ray_z /= ray_len;

            // Convert ray to fisheye coordinates
            float theta = std::acos(ray_z);
            float phi   = std::atan2(ray_y, ray_x);

            // Check if point is within fisheye FOV
            if (theta > fov_rad / 2.0f) {
                map_x_.at<float>(y, x) = -1.0f;
                map_y_.at<float>(y, x) = -1.0f;
                continue;
            }

            // Map theta to radius in fisheye image
            float r = (theta / (fov_rad / 2.0f)) * radius;

            // Convert polar to cartesian in fisheye image
            float fish_x = cx + r * std::cos(phi);
            float fish_y = cy + r * std::sin(phi);

            map_x_.at<float>(y, x) = fish_x;
            map_y_.at<float>(y, x) = fish_y;
        }
    }
}

void Dewarper::build_fisheye_undistort_map() {
    // Manual implementation of OpenCV's fisheye::initUndistortRectifyMap
    // This matches the Python cv2.fisheye.initUndistortRectifyMap behavior exactly

    map_x_.create(config_.output_height, config_.output_width, CV_32FC1);
    map_y_.create(config_.output_height, config_.output_width, CV_32FC1);

    float f_in = config_.focal_length;
    float f_out = f_in * config_.scale;

    // Input camera: optical center in input image
    float cx_in = config_.center_x * config_.input_width;
    float cy_in = config_.center_y * config_.input_height;

    // Output camera: use same optical center as input (matches Python CONTROLLED mode)
    float cx_out = cx_in;
    float cy_out = cy_in;

    syslog(LOG_INFO,
           "Fisheye undistort: f_in=%.2f, f_out=%.2f, cx_in=%.2f, cy_in=%.2f, cx_out=%.2f, "
           "cy_out=%.2f",
           f_in,
           f_out,
           cx_in,
           cy_in,
           cx_out,
           cy_out);
    syslog(LOG_INFO, "Fisheye undistort: k=(%.4f,%.4f,%.4f,%.4f)", config_.k1, config_.k2, config_.k3, config_.k4);

    // For each output pixel, compute corresponding input pixel
    for (unsigned int v_out = 0; v_out < config_.output_height; v_out++) {
        for (unsigned int u_out = 0; u_out < config_.output_width; u_out++) {
            // Convert output pixel to normalized camera coordinates
            float x_out = (static_cast<float>(u_out) - cx_out) / f_out;
            float y_out = (static_cast<float>(v_out) - cy_out) / f_out;

            // Debug: Log specific test pixels to compare with OpenCV
            bool is_test_pixel = (u_out == 1496 && v_out == 1496) ||
                                  (u_out == 1496 && v_out == 1000) ||
                                  (u_out == 2000 && v_out == 1496) ||
                                  (u_out == 1496 && v_out == 2000);

            // Compute angle theta from optical axis
            // For fisheye equidistant model: theta = r (not atan(r))
            float r_out = std::sqrt(x_out * x_out + y_out * y_out);
            float theta = r_out;

            // Apply fisheye distortion polynomial to get distorted angle
            // For fisheye model: theta_d = theta * (1 + k1*theta^2 + k2*theta^4 + k3*theta^6 + k4*theta^8)
            float theta2 = theta * theta;
            float theta4 = theta2 * theta2;
            float theta6 = theta4 * theta2;
            float theta8 = theta6 * theta2;
            float theta_d = theta * (1.0f + config_.k1 * theta2 + config_.k2 * theta4 +
                                     config_.k3 * theta6 + config_.k4 * theta8);

            // Convert back to normalized distorted coordinates
            float scale_d = (r_out > 1e-8f) ? (theta_d / r_out) : 1.0f;
            float x_dist = x_out * scale_d;
            float y_dist = y_out * scale_d;

            // Convert to input image pixel coordinates
            float u_in = f_in * x_dist + cx_in;
            float v_in = f_in * y_dist + cy_in;

            map_x_.at<float>(v_out, u_out) = u_in;
            map_y_.at<float>(v_out, u_out) = v_in;

            // Debug logging for test pixels
            if (is_test_pixel) {
                syslog(LOG_INFO, "TEST: Output (%u, %u) -> Input (%.2f, %.2f)",
                       u_out, v_out, u_in, v_in);
            }
        }
    }

    syslog(LOG_INFO,
           "Fisheye undistort map built: %ux%u -> %ux%u",
           config_.input_width,
           config_.input_height,
           config_.output_width,
           config_.output_height);
}

LensType Dewarper::parse_lens_type(const std::string& str) {
    if (str == "dual_fisheye") {
        return LensType::DUAL_FISHEYE;
    } else if (str == "panoramic") {
        return LensType::PANORAMIC;
    }
    return LensType::FISHEYE;
}

ProjectionType Dewarper::parse_projection_type(const std::string& str) {
    if (str == "rectilinear") {
        return ProjectionType::RECTILINEAR;
    } else if (str == "cylindrical") {
        return ProjectionType::CYLINDRICAL;
    } else if (str == "fisheye_undistort") {
        return ProjectionType::FISHEYE_UNDISTORT;
    }
    return ProjectionType::FISHEYE_UNDISTORT;
}

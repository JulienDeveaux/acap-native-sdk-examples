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

#pragma once

#include <opencv2/core.hpp>
#include <string>

enum class LensType { FISHEYE, DUAL_FISHEYE, PANORAMIC };

enum class ProjectionType { EQUIRECTANGULAR, RECTILINEAR, CYLINDRICAL };

struct DewarperConfig {
    LensType lens_type           = LensType::FISHEYE;
    ProjectionType projection    = ProjectionType::EQUIRECTANGULAR;
    float input_fov              = 180.0f;
    unsigned int input_width     = 0;
    unsigned int input_height    = 0;
    unsigned int output_width    = 1920;
    unsigned int output_height   = 1080;
    float center_x               = 0.5f;
    float center_y               = 0.5f;
    float pan_angle              = 0.0f;
    float tilt_angle             = 0.0f;
    float rectilinear_fov        = 90.0f;
};

class Dewarper {
public:
    Dewarper();
    ~Dewarper();

    // Initialize with configuration
    bool init(const DewarperConfig& config);

    // Process a frame - input is NV12, output is BGR
    bool process(const cv::Mat& input_nv12, cv::Mat& output_bgr);

    // Update configuration (rebuilds lookup tables)
    bool update_config(const DewarperConfig& config);

    // Get current configuration
    const DewarperConfig& get_config() const { return config_; }

    // Parse configuration from string values
    static LensType parse_lens_type(const std::string& str);
    static ProjectionType parse_projection_type(const std::string& str);

private:
    void build_lookup_tables();
    void build_equirectangular_map();
    void build_rectilinear_map();
    void build_cylindrical_map();

    DewarperConfig config_;
    cv::Mat map_x_;
    cv::Mat map_y_;
    cv::Mat bgr_temp_;
    bool initialized_ = false;
};

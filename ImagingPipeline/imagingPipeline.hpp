// Copyright (c) 2021-2022 Glass Imaging Inc.
// Author: Fabio Riccardi <fabio@glass-imaging.com>
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef imagingPipeline_hpp
#define imagingPipeline_hpp

#include <filesystem>
#include <string>
#include <cmath>
#include <chrono>

#include "gls_logging.h"
#include "gls_image.hpp"

#include "raw_converter.hpp"

#include "demosaic.hpp"
#include "gls_tiff_metadata.hpp"

#include "gls_linalg.hpp"

#include "CameraCalibration.hpp"

void copyMetadata(const gls::tiff_metadata& source, gls::tiff_metadata* destination, ttag_t tag);

void saveStrippedDNG(const std::string& file_name, const gls::image<gls::luma_pixel_16>& inputImage, const gls::tiff_metadata& dng_metadata, const gls::tiff_metadata& exif_metadata);

void transcodeAdobeDNG(const std::filesystem::path& input_path);

gls::image<gls::rgb_pixel>::unique_ptr demosaicPlainFile(RawConverter* rawConverter, const std::filesystem::path& input_path);

void processKodakSet(gls::OpenCLContext* glsContext, const std::filesystem::path& input_path);

void demosaicFile(RawConverter* rawConverter, std::filesystem::path input_path);

void demosaicDirectory(RawConverter* rawConverter, std::filesystem::path input_path);

#endif /* imagingPipeline_hpp */

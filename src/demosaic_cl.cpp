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

#include <float.h>

#include <cmath>
#include <iomanip>

#include "RTL/RTL.hpp"
#include "demosaic.hpp"
#include "gls_cl.hpp"
#include "gls_cl_image.hpp"
#include "gls_logging.h"
#include "gls_statistics.hpp"

static const char* TAG = "DEMOSAIC";

/*
 OpenCL RAW Image Demosaic.
 NOTE: This code can throw exceptions, to facilitate debugging no exception handler is provided, so things can crash in
 place.
 */

void scaleRawData(gls::OpenCLContext* glsContext, const gls::cl_image_2d<gls::luma_pixel_16>& rawImage,
                  gls::cl_image_2d<gls::luma_pixel_float>* scaledRawImage, BayerPattern bayerPattern,
                  gls::Vector<4> scaleMul, float blackLevel) {
    // Load the shader source
    const auto program = glsContext->loadProgram("demosaic");

    // Bind the kernel parameters
    auto kernel = cl::KernelFunctor<cl::Image2D,  // rawImage
                                    cl::Image2D,  // scaledRawImage
                                    int,          // bayerPattern
                                    cl_float4,    // scaleMul
                                    float         // blackLevel
                                    >(program, "scaleRawData");

    // Work on one Quad (2x2) at a time
    kernel(gls::OpenCLContext::buildEnqueueArgs(scaledRawImage->width / 2, scaledRawImage->height / 2),
           rawImage.getImage2D(), scaledRawImage->getImage2D(), bayerPattern,
           {scaleMul[0], scaleMul[1], scaleMul[2], scaleMul[3]}, blackLevel);
}

void rawImageGradient(gls::OpenCLContext* glsContext, const gls::cl_image_2d<gls::luma_pixel_float>& rawImage,
                      gls::cl_image_2d<gls::luma_alpha_pixel_float>* gradientImage) {
    // Load the shader source
    const auto program = glsContext->loadProgram("demosaic");

    // Bind the kernel parameters
    auto kernel = cl::KernelFunctor<cl::Image2D,  // rawImage
                                    cl::Image2D   // gradientImage
                                    >(program, "rawImageGradient");

    // Schedule the kernel on the GPU
    kernel(gls::OpenCLContext::buildEnqueueArgs(gradientImage->width, gradientImage->height), rawImage.getImage2D(),
           gradientImage->getImage2D());
}

void rawImageSobel(gls::OpenCLContext* glsContext, const gls::cl_image_2d<gls::luma_pixel_float>& rawImage,
                   gls::cl_image_2d<gls::rgba_pixel_float>* gradientImage) {
    // Load the shader source
    const auto program = glsContext->loadProgram("demosaic");

    // Bind the kernel parameters
    auto kernel = cl::KernelFunctor<cl::Image2D,  // rawImage
                                    cl::Image2D   // gradientImage
                                    >(program, "rawImageSobel");

    // Schedule the kernel on the GPU
    kernel(gls::OpenCLContext::buildEnqueueArgs(gradientImage->width, gradientImage->height), rawImage.getImage2D(),
           gradientImage->getImage2D());
}

void interpolateGreen(gls::OpenCLContext* glsContext, const gls::cl_image_2d<gls::luma_pixel_float>& rawImage,
                      const gls::cl_image_2d<gls::luma_alpha_pixel_float>& gradientImage,
                      gls::cl_image_2d<gls::luma_pixel_float>* greenImage, BayerPattern bayerPattern,
                      gls::Vector<2> greenVariance) {
    // Load the shader source
    const auto program = glsContext->loadProgram("demosaic");

    // Bind the kernel parameters
    auto kernel = cl::KernelFunctor<cl::Image2D,  // rawImage
                                    cl::Image2D,  // gradientImage
                                    cl::Image2D,  // greenImage
                                    int,          // bayerPattern
                                    cl_float2     // greenVariance
                                    >(program, "interpolateGreen");

    // Schedule the kernel on the GPU
    kernel(gls::OpenCLContext::buildEnqueueArgs(greenImage->width, greenImage->height), rawImage.getImage2D(),
           gradientImage.getImage2D(), greenImage->getImage2D(), bayerPattern, {greenVariance[0], greenVariance[1]});
}

void interpolateRedBlue(gls::OpenCLContext* glsContext, const gls::cl_image_2d<gls::luma_pixel_float>& rawImage,
                        const gls::cl_image_2d<gls::luma_pixel_float>& greenImage,
                        const gls::cl_image_2d<gls::luma_alpha_pixel_float>& gradientImage,
                        gls::cl_image_2d<gls::rgba_pixel_float>* rgbImage, BayerPattern bayerPattern,
                        gls::Vector<2> redVariance, gls::Vector<2> blueVariance) {
    // Load the shader source
    const auto program = glsContext->loadProgram("demosaic");

    // Bind the kernel parameters
    auto kernel = cl::KernelFunctor<cl::Image2D,  // rawImage
                                    cl::Image2D,  // greenImage
                                    cl::Image2D,  // gradientImage
                                    cl::Image2D,  // rgbImage
                                    int,          // bayerPattern
                                    cl_float2,    // redVariance
                                    cl_float2     // blueVariance
                                    >(program, "interpolateRedBlue");

    // Schedule the kernel on the GPU
    kernel(gls::OpenCLContext::buildEnqueueArgs(rgbImage->width / 2, rgbImage->height / 2), rawImage.getImage2D(),
           greenImage.getImage2D(), gradientImage.getImage2D(), rgbImage->getImage2D(), bayerPattern,
           {redVariance[0], redVariance[1]}, {blueVariance[0], blueVariance[1]});
}

void interpolateRedBlueAtGreen(gls::OpenCLContext* glsContext,
                               const gls::cl_image_2d<gls::rgba_pixel_float>& rgbImageIn,
                               const gls::cl_image_2d<gls::luma_alpha_pixel_float>& gradientImage,
                               gls::cl_image_2d<gls::rgba_pixel_float>* rgbImageOut, BayerPattern bayerPattern,
                               gls::Vector<2> redVariance, gls::Vector<2> blueVariance) {
    // Load the shader source
    const auto program = glsContext->loadProgram("demosaic");

    // Bind the kernel parameters
    auto kernel = cl::KernelFunctor<cl::Image2D,  // rgbImageIn
                                    cl::Image2D,  // gradientImage
                                    cl::Image2D,  // rgbImageOut
                                    int,          // bayerPattern
                                    cl_float2,    // redVariance
                                    cl_float2     // blueVariance
                                    >(program, "interpolateRedBlueAtGreen");

    // Schedule the kernel on the GPU
    kernel(gls::OpenCLContext::buildEnqueueArgs(rgbImageOut->width / 2, rgbImageOut->height / 2),
           rgbImageIn.getImage2D(), gradientImage.getImage2D(), rgbImageOut->getImage2D(), bayerPattern,
           {redVariance[0], redVariance[1]}, {blueVariance[0], blueVariance[1]});
}

void malvar(gls::OpenCLContext* glsContext, const gls::cl_image_2d<gls::luma_pixel_float>& rawImage,
            const gls::cl_image_2d<gls::luma_alpha_pixel_float>& gradientImage,
            gls::cl_image_2d<gls::rgba_pixel_float>* rgbImage, BayerPattern bayerPattern, gls::Vector<2> redVariance,
            gls::Vector<2> greenVariance, gls::Vector<2> blueVariance) {
    // Load the shader source
    const auto program = glsContext->loadProgram("demosaic");

    // Bind the kernel parameters
    auto kernel = cl::KernelFunctor<cl::Image2D,  // rawImage
                                    cl::Image2D,  // gradientImage
                                    cl::Image2D,  // rgbImage
                                    int,          // bayerPattern
                                    cl_float2,    // redVariance
                                    cl_float2,    // greenVariance
                                    cl_float2     // blueVariance
                                    >(program, "malvar");

    // Schedule the kernel on the GPU
    kernel(gls::OpenCLContext::buildEnqueueArgs(rgbImage->width, rgbImage->height), rawImage.getImage2D(),
           gradientImage.getImage2D(), rgbImage->getImage2D(), bayerPattern, {redVariance[0], redVariance[1]},
           {greenVariance[0], greenVariance[1]}, {blueVariance[0], blueVariance[1]});
}

void fasteDebayer(gls::OpenCLContext* glsContext, const gls::cl_image_2d<gls::luma_pixel_float>& rawImage,
                  gls::cl_image_2d<gls::rgba_pixel_float>* rgbImage, BayerPattern bayerPattern) {
    assert(rawImage.width == 2 * rgbImage->width && rawImage.height == 2 * rgbImage->height);

    // Load the shader source
    const auto program = glsContext->loadProgram("demosaic");

    // Bind the kernel parameters
    auto kernel = cl::KernelFunctor<cl::Image2D,  // rawImage
                                    cl::Image2D,  // rgbImage
                                    int           // bayerPattern
                                    >(program, "fastDebayer");

    // Schedule the kernel on the GPU
    kernel(gls::OpenCLContext::buildEnqueueArgs(rgbImage->width, rgbImage->height), rawImage.getImage2D(),
           rgbImage->getImage2D(), bayerPattern);
}

void YCbCrNoiseStatistics(gls::OpenCLContext* glsContext, const gls::cl_image_2d<gls::rgba_pixel_float>& inputImage,
                          const gls::cl_image_2d<gls::luma_alpha_pixel_float>& sobelImage,
                          gls::cl_image_2d<gls::rgba_pixel_float>* statsImage) {
    // Load the shader source
    const auto program = glsContext->loadProgram("demosaic");

    // Bind the kernel parameters
    auto kernel = cl::KernelFunctor<cl::Image2D,  // inputImage
                                    cl::Image2D,  // sobelImage
                                    cl::Image2D   // statsImage
                                    >(program, "YCbCrNoiseStatistics");

    // Schedule the kernel on the GPU
    kernel(gls::OpenCLContext::buildEnqueueArgs(statsImage->width, statsImage->height), inputImage.getImage2D(),
           sobelImage.getImage2D(), statsImage->getImage2D());
}

void rawNoiseStatistics(gls::OpenCLContext* glsContext, const gls::cl_image_2d<gls::luma_pixel_float>& rawImage,
                        BayerPattern bayerPattern, const gls::cl_image_2d<gls::rgba_pixel_float>& sobelImage,
                        gls::cl_image_2d<gls::rgba_pixel_float>* meanImage,
                        gls::cl_image_2d<gls::rgba_pixel_float>* varImage,
                        gls::cl_image_2d<gls::rgba_pixel_float>* kurtImage) {
    assert(rawImage.width == 2 * meanImage->width && rawImage.height == 2 * meanImage->height);
    assert(varImage->width == meanImage->width && varImage->height == meanImage->height);
    assert(kurtImage->width == meanImage->width && kurtImage->height == meanImage->height);

    // Load the shader source
    const auto program = glsContext->loadProgram("demosaic");

    // Bind the kernel parameters
    auto kernel = cl::KernelFunctor<cl::Image2D,  // rawImage
                                    int,          // bayerPattern
                                    cl::Image2D,  // sobelImage
                                    cl::Image2D,  // meanImage
                                    cl::Image2D,  // varImage
                                    cl::Image2D   // kurtImage
                                    >(program, "rawNoiseStatistics");

    // Schedule the kernel on the GPU
    kernel(gls::OpenCLContext::buildEnqueueArgs(meanImage->width, meanImage->height), rawImage.getImage2D(),
           bayerPattern, sobelImage.getImage2D(), meanImage->getImage2D(), varImage->getImage2D(),
           kurtImage->getImage2D());
}

template <typename T1, typename T2>
void applyKernel(gls::OpenCLContext* glsContext, const std::string& kernelName, const gls::cl_image_2d<T1>& inputImage,
                 gls::cl_image_2d<T2>* outputImage) {
    // Load the shader source
    const auto program = glsContext->loadProgram("demosaic");

    // Bind the kernel parameters
    auto kernel = cl::KernelFunctor<cl::Image2D,  // inputImage
                                    cl::Image2D   // outputImage
                                    >(program, kernelName);

    // Schedule the kernel on the GPU
    kernel(gls::OpenCLContext::buildEnqueueArgs(outputImage->width, outputImage->height), inputImage.getImage2D(),
           outputImage->getImage2D());
}

template void applyKernel(gls::OpenCLContext* glsContext, const std::string& kernelName,
                          const gls::cl_image_2d<gls::luma_pixel_float>& inputImage,
                          gls::cl_image_2d<gls::luma_pixel_float>* outputImage);

template void applyKernel(gls::OpenCLContext* glsContext, const std::string& kernelName,
                          const gls::cl_image_2d<gls::luma_alpha_pixel_float>& inputImage,
                          gls::cl_image_2d<gls::luma_alpha_pixel_float>* outputImage);

template void applyKernel(gls::OpenCLContext* glsContext, const std::string& kernelName,
                          const gls::cl_image_2d<gls::rgba_pixel_float>& inputImage,
                          gls::cl_image_2d<gls::rgba_pixel_float>* outputImage);

template void applyKernel(gls::OpenCLContext* glsContext, const std::string& kernelName,
                          const gls::cl_image_2d<gls::luma_pixel_float>& inputImage,
                          gls::cl_image_2d<gls::rgba_pixel_float>* outputImage);

template <typename T>
void resampleImage(gls::OpenCLContext* glsContext, const std::string& kernelName, const gls::cl_image_2d<T>& inputImage,
                   gls::cl_image_2d<T>* outputImage) {
    // Load the shader source
    const auto program = glsContext->loadProgram("demosaic");

    const auto linear_sampler = cl::Sampler(glsContext->clContext(), true, CL_ADDRESS_CLAMP_TO_EDGE, CL_FILTER_LINEAR);

    // Bind the kernel parameters
    auto kernel = cl::KernelFunctor<cl::Image2D,  // inputImage
                                    cl::Image2D,  // outputImage
                                    cl::Sampler>(program, kernelName);

    // Schedule the kernel on the GPU
    kernel(gls::OpenCLContext::buildEnqueueArgs(outputImage->width, outputImage->height), inputImage.getImage2D(),
           outputImage->getImage2D(), linear_sampler);
}

template void resampleImage(gls::OpenCLContext* glsContext, const std::string& kernelName,
                            const gls::cl_image_2d<gls::rgba_pixel_float>& inputImage,
                            gls::cl_image_2d<gls::rgba_pixel_float>* outputImage);

template void resampleImage(gls::OpenCLContext* glsContext, const std::string& kernelName,
                            const gls::cl_image_2d<gls::luma_alpha_pixel_float>& inputImage,
                            gls::cl_image_2d<gls::luma_alpha_pixel_float>* outputImage);

template <typename T>
void subtractNoiseImage(gls::OpenCLContext* glsContext, const gls::cl_image_2d<T>& inputImage,
                        const gls::cl_image_2d<T>& inputImage1, const gls::cl_image_2d<T>& inputImageDenoised1,
                        const gls::cl_image_2d<gls::luma_alpha_pixel_float>& gradientImage, float luma_weight,
                        float sharpening, const gls::Vector<2>& nlf, gls::cl_image_2d<T>* outputImage) {
    // Load the shader source
    const auto program = glsContext->loadProgram("demosaic");

    const auto linear_sampler = cl::Sampler(glsContext->clContext(), true, CL_ADDRESS_CLAMP_TO_EDGE, CL_FILTER_LINEAR);

    // Bind the kernel parameters
    auto kernel = cl::KernelFunctor<cl::Image2D,  // inputImage
                                    cl::Image2D,  // inputImage1
                                    cl::Image2D,  // inputImageDenoised1
                                    cl::Image2D,  // gradientImage
                                    float,        // luma_weight
                                    float,        // sharpening
                                    cl_float2,    // nlf
                                    cl::Image2D,  // outputImage
                                    cl::Sampler   // linear_sampler
                                    >(program, "subtractNoiseImage");

    // Schedule the kernel on the GPU
    kernel(gls::OpenCLContext::buildEnqueueArgs(outputImage->width, outputImage->height), inputImage.getImage2D(),
           inputImage1.getImage2D(), inputImageDenoised1.getImage2D(), gradientImage.getImage2D(), luma_weight,
           sharpening, {nlf[0], nlf[1]}, outputImage->getImage2D(), linear_sampler);
}

template void subtractNoiseImage(gls::OpenCLContext* glsContext,
                                 const gls::cl_image_2d<gls::rgba_pixel_float>& inputImage,
                                 const gls::cl_image_2d<gls::rgba_pixel_float>& inputImage1,
                                 const gls::cl_image_2d<gls::rgba_pixel_float>& inputImageDenoised1,
                                 const gls::cl_image_2d<gls::luma_alpha_pixel_float>& gradientImage, float luma_weight,
                                 float sharpening, const gls::Vector<2>& nlf,
                                 gls::cl_image_2d<gls::rgba_pixel_float>* outputImage);

void transformImage(gls::OpenCLContext* glsContext, const gls::cl_image_2d<gls::rgba_pixel_float>& linearImage,
                    gls::cl_image_2d<gls::rgba_pixel_float>* rgbImage, const gls::Matrix<3, 3>& transform) {
    // Load the shader source
    const auto program = glsContext->loadProgram("demosaic");

    struct Matrix3x3 {
        cl_float3 m[3];
    } clTransform = {{{transform[0][0], transform[0][1], transform[0][2]},
                      {transform[1][0], transform[1][1], transform[1][2]},
                      {transform[2][0], transform[2][1], transform[2][2]}}};

    // Bind the kernel parameters
    auto kernel = cl::KernelFunctor<cl::Image2D,  // linearImage
                                    cl::Image2D,  // rgbImage
                                    Matrix3x3     // transform
                                    >(program, "transformImage");

    // Schedule the kernel on the GPU
    kernel(gls::OpenCLContext::buildEnqueueArgs(rgbImage->width, rgbImage->height), linearImage.getImage2D(),
           rgbImage->getImage2D(), clTransform);
}

void convertTosRGB(gls::OpenCLContext* glsContext, const gls::cl_image_2d<gls::rgba_pixel_float>& linearImage,
                   const gls::cl_image_2d<gls::luma_pixel_float>& ltmMaskImage,
                   gls::cl_image_2d<gls::rgba_pixel_float>* rgbImage, const DemosaicParameters& demosaicParameters) {
    // Load the shader source
    const auto program = glsContext->loadProgram("demosaic");

    const auto& transform = demosaicParameters.rgb_cam;

    struct Matrix3x3 {
        cl_float3 m[3];
    } clTransform = {{{transform[0][0], transform[0][1], transform[0][2]},
                      {transform[1][0], transform[1][1], transform[1][2]},
                      {transform[2][0], transform[2][1], transform[2][2]}}};

    // Bind the kernel parameters
    auto kernel = cl::KernelFunctor<cl::Image2D,             // linearImage
                                    cl::Image2D,             // ltmMaskImage
                                    cl::Image2D,             // rgbImage
                                    Matrix3x3,               // transform
                                    RGBConversionParameters  // demosaicParameters
                                    >(program, "convertTosRGB");

    // Schedule the kernel on the GPU
    kernel(gls::OpenCLContext::buildEnqueueArgs(rgbImage->width, rgbImage->height), linearImage.getImage2D(),
           ltmMaskImage.getImage2D(), rgbImage->getImage2D(), clTransform, demosaicParameters.rgbConversionParameters);
}

void convertToGrayscale(gls::OpenCLContext* glsContext, const gls::cl_image_2d<gls::rgba_pixel_float>& linearImage,
                        gls::cl_image_2d<float>* grayscaleImage, const DemosaicParameters& demosaicParameters) {
    // Load the shader source
    const auto program = glsContext->loadProgram("demosaic");

    const auto& transform = demosaicParameters.rgb_cam;

    // Bind the kernel parameters
    auto kernel = cl::KernelFunctor<cl::Image2D,  // linearImage
                                    cl::Image2D,  // grayscaleImage
                                    cl_float3     // transform
                                    >(program, "convertToGrayscale");

    // Schedule the kernel on the GPU
    kernel(gls::OpenCLContext::buildEnqueueArgs(grayscaleImage->width, grayscaleImage->height),
           linearImage.getImage2D(), grayscaleImage->getImage2D(), {transform[0][0], transform[0][1], transform[0][2]});
}

void despeckleImage(gls::OpenCLContext* glsContext, const gls::cl_image_2d<gls::rgba_pixel_float>& inputImage,
                    const gls::Vector<3>& var_a, const gls::Vector<3>& var_b,
                    gls::cl_image_2d<gls::rgba_pixel_float>* outputImage) {
    // Load the shader source
    const auto program = glsContext->loadProgram("demosaic");

    // Bind the kernel parameters
    auto kernel = cl::KernelFunctor<cl::Image2D,  // inputImage
                                    cl_float3,    // var_a
                                    cl_float3,    // var_b
                                    cl::Image2D   // outputImage
                                    >(program, "despeckleLumaMedianChromaImage");

    cl_float3 cl_var_a = {var_a[0], var_a[1], var_a[2]};
    cl_float3 cl_var_b = {var_b[0], var_b[1], var_b[2]};

    // Schedule the kernel on the GPU
    kernel(gls::OpenCLContext::buildEnqueueArgs(outputImage->width, outputImage->height), inputImage.getImage2D(),
           cl_var_a, cl_var_b, outputImage->getImage2D());
}

// --- Multiscale Noise Reduction ---
// https://www.cns.nyu.edu/pub/lcv/rajashekar08a.pdf

void denoiseImage(gls::OpenCLContext* glsContext, const gls::cl_image_2d<gls::rgba_pixel_float>& inputImage,
                  const gls::cl_image_2d<gls::luma_alpha_pixel_float>& gradientImage, const gls::Vector<3>& var_a,
                  const gls::Vector<3>& var_b, const gls::Vector<3> thresholdMultipliers, float chromaBoost,
                  float gradientBoost, float gradientThreshold, gls::cl_image_2d<gls::rgba_pixel_float>* outputImage) {
    // Load the shader source
    const auto program = glsContext->loadProgram("demosaic");

    // Bind the kernel parameters
    auto kernel = cl::KernelFunctor<cl::Image2D,  // inputImage
                                    cl::Image2D,  // gradientImage
                                    cl_float3,    // var_a
                                    cl_float3,    // var_b
                                    cl_float3,    // thresholdMultipliers
                                    float,        // chromaBoost
                                    float,        // gradientBoost
                                    float,        // gradientThreshold
                                    cl::Image2D   // outputImage
                                    >(program, "denoiseImage");

    cl_float3 cl_var_a = {var_a[0], var_a[1], var_a[2]};
    cl_float3 cl_var_b = {var_b[0], var_b[1], var_b[2]};

    // Schedule the kernel on the GPU
    kernel(gls::OpenCLContext::buildEnqueueArgs(outputImage->width, outputImage->height), inputImage.getImage2D(),
           gradientImage.getImage2D(), cl_var_a, cl_var_b,
           {thresholdMultipliers[0], thresholdMultipliers[1], thresholdMultipliers[2]}, chromaBoost, gradientBoost,
           gradientThreshold, outputImage->getImage2D());
}

void denoiseImageGuided(gls::OpenCLContext* glsContext, const gls::cl_image_2d<gls::rgba_pixel_float>& inputImage,
                        const gls::Vector<3>& var_a, const gls::Vector<3>& var_b,
                        gls::cl_image_2d<gls::rgba_pixel_float>* outputImage) {
    // Load the shader source
    const auto program = glsContext->loadProgram("demosaic");

    // Bind the kernel parameters
    auto kernel = cl::KernelFunctor<cl::Image2D,  // inputImage
                                    cl_float3,    // var_a
                                    cl_float3,    // ver_b
                                    cl::Image2D   // outputImage
                                    >(program, "denoiseImageGuided");

    cl_float3 cl_var_a = {var_a[0], var_a[1], var_a[2]};
    cl_float3 cl_var_b = {var_b[0], var_b[1], var_b[2]};

    // Schedule the kernel on the GPU
    kernel(gls::OpenCLContext::buildEnqueueArgs(outputImage->width, outputImage->height), inputImage.getImage2D(),
           cl_var_a, cl_var_b, outputImage->getImage2D());
}

// Arrays order is LF, MF, HF
void localToneMappingMask(gls::OpenCLContext* glsContext, const gls::cl_image_2d<gls::rgba_pixel_float>& inputImage,
                          const std::array<const gls::cl_image_2d<gls::rgba_pixel_float>*, 3>& guideImage,
                          const std::array<const gls::cl_image_2d<gls::luma_alpha_pixel_float>*, 3>& abImage,
                          const std::array<const gls::cl_image_2d<gls::luma_alpha_pixel_float>*, 3>& abMeanImage,
                          const LTMParameters& ltmParameters, const gls::Matrix<3, 3>& ycbcr_srgb,
                          const gls::Vector<2>& nlf, gls::cl_image_2d<gls::luma_pixel_float>* outputImage) {
    for (int i = 0; i < 3; i++) {
        assert(guideImage[i]->width == abImage[i]->width && guideImage[i]->height == abImage[i]->height);
        assert(guideImage[i]->width == abMeanImage[i]->width && guideImage[i]->height == abMeanImage[i]->height);
    }

    // Load the shader source
    const auto program = glsContext->loadProgram("demosaic");

    struct Matrix3x3 {
        cl_float3 m[3];
    } cl_ycbcr_srgb = {{{ycbcr_srgb[0][0], ycbcr_srgb[0][1], ycbcr_srgb[0][2]},
                        {ycbcr_srgb[1][0], ycbcr_srgb[1][1], ycbcr_srgb[1][2]},
                        {ycbcr_srgb[2][0], ycbcr_srgb[2][1], ycbcr_srgb[2][2]}}};

    const auto linear_sampler = cl::Sampler(glsContext->clContext(), true, CL_ADDRESS_CLAMP_TO_EDGE, CL_FILTER_LINEAR);

    // Bind the kernel parameters
    auto gfKernel = cl::KernelFunctor<cl::Image2D,  // guideImage
                                      cl::Image2D,  // abImage
                                      float,        // eps
                                      cl::Sampler   // linear_sampler
                                      >(program, "GuidedFilterABImage");

    auto gfMeanKernel = cl::KernelFunctor<cl::Image2D,  // inputImage
                                          cl::Image2D,  // outputImage
                                          cl::Sampler   // linear_sampler
                                          >(program, "BoxFilterGFImage");

    auto ltmKernel = cl::KernelFunctor<cl::Image2D,    // inputImage
                                       cl::Image2D,    // lfAbImage
                                       cl::Image2D,    // mfAbImage
                                       cl::Image2D,    // hfAbImage
                                       cl::Image2D,    // ltmMaskImage
                                       LTMParameters,  // ltmParameters
                                       Matrix3x3,      // ycbcr_srgb
                                       cl_float2,      // nlf
                                       cl::Sampler     // linear_sampler
                                       >(program, "localToneMappingMaskImage");

    // Schedule the kernel on the GPU
    for (int i = 0; i < 3; i++) {
        if (i == 0 || ltmParameters.detail[i] != 1) {
            gfKernel(gls::OpenCLContext::buildEnqueueArgs(guideImage[i]->width, guideImage[i]->height),
                     guideImage[i]->getImage2D(), abImage[i]->getImage2D(), ltmParameters.eps, linear_sampler);

            gfMeanKernel(gls::OpenCLContext::buildEnqueueArgs(abImage[i]->width, abImage[i]->height),
                         abImage[i]->getImage2D(), abMeanImage[i]->getImage2D(), linear_sampler);
        }
    }

    ltmKernel(gls::OpenCLContext::buildEnqueueArgs(outputImage->width, outputImage->height), inputImage.getImage2D(),
              abMeanImage[0]->getImage2D(), abMeanImage[1]->getImage2D(), abMeanImage[2]->getImage2D(),
              outputImage->getImage2D(), ltmParameters, cl_ycbcr_srgb, {nlf[0], nlf[1]}, linear_sampler);
}

void bayerToRawRGBA(gls::OpenCLContext* glsContext, const gls::cl_image_2d<gls::luma_pixel_float>& rawImage,
                    gls::cl_image_2d<gls::rgba_pixel_float>* rgbaImage, BayerPattern bayerPattern) {
    assert(rawImage.width == 2 * rgbaImage->width && rawImage.height == 2 * rgbaImage->height);

    // Load the shader source
    const auto program = glsContext->loadProgram("demosaic");

    // Bind the kernel parameters
    auto kernel = cl::KernelFunctor<cl::Image2D,  // rawImage
                                    cl::Image2D,  // rgbaImage
                                    int           // bayerPattern
                                    >(program, "bayerToRawRGBA");

    // Schedule the kernel on the GPU
    kernel(gls::OpenCLContext::buildEnqueueArgs(rgbaImage->width, rgbaImage->height), rawImage.getImage2D(),
           rgbaImage->getImage2D(), bayerPattern);
}

void rawRGBAToBayer(gls::OpenCLContext* glsContext, const gls::cl_image_2d<gls::rgba_pixel_float>& rgbaImage,
                    gls::cl_image_2d<gls::luma_pixel_float>* rawImage, BayerPattern bayerPattern) {
    assert(rawImage->width == 2 * rgbaImage.width && rawImage->height == 2 * rgbaImage.height);

    // Load the shader source
    const auto program = glsContext->loadProgram("demosaic");

    // Bind the kernel parameters
    auto kernel = cl::KernelFunctor<cl::Image2D,  // rgbaImage
                                    cl::Image2D,  // rawImage
                                    int           // bayerPattern
                                    >(program, "rawRGBAToBayer");

    // Schedule the kernel on the GPU
    kernel(gls::OpenCLContext::buildEnqueueArgs(rgbaImage.width, rgbaImage.height), rgbaImage.getImage2D(),
           rawImage->getImage2D(), bayerPattern);
}

void denoiseRawRGBAImage(gls::OpenCLContext* glsContext, const gls::cl_image_2d<gls::rgba_pixel_float>& inputImage,
                         const gls::Vector<4> rawVariance, gls::cl_image_2d<gls::rgba_pixel_float>* outputImage) {
    // Load the shader source
    const auto program = glsContext->loadProgram("demosaic");

    // Bind the kernel parameters
    auto kernel = cl::KernelFunctor<cl::Image2D,  // inputImage
                                    cl_float4,    // rawVariance
                                    cl::Image2D   // outputImage
                                    >(program, "denoiseRawRGBAImage");

    // Schedule the kernel on the GPU
    kernel(gls::OpenCLContext::buildEnqueueArgs(outputImage->width, outputImage->height), inputImage.getImage2D(),
           {rawVariance[0], rawVariance[1], rawVariance[2], rawVariance[3]}, outputImage->getImage2D());
}

void despeckleRawRGBAImage(gls::OpenCLContext* glsContext, const gls::cl_image_2d<gls::rgba_pixel_float>& inputImage,
                           const gls::Vector<4> rawVariance, gls::cl_image_2d<gls::rgba_pixel_float>* outputImage) {
    // Load the shader source
    const auto program = glsContext->loadProgram("demosaic");

    // Bind the kernel parameters
    auto kernel = cl::KernelFunctor<cl::Image2D,  // inputImage
                                    cl_float4,    // rawVariance
                                    cl::Image2D   // outputImage
                                    >(program, "despeckleRawRGBAImage");

    // Schedule the kernel on the GPU
    kernel(gls::OpenCLContext::buildEnqueueArgs(outputImage->width, outputImage->height), inputImage.getImage2D(),
           {rawVariance[0], rawVariance[1], rawVariance[2], rawVariance[3]}, outputImage->getImage2D());
}

std::vector<std::array<float, 3>> gaussianKernelBilinearWeights(float radius) {
    int kernelSize = (int)(ceil(2 * radius));
    if ((kernelSize % 2) == 0) {
        kernelSize++;
    }

    std::vector<float> weights(kernelSize * kernelSize);
    for (int y = -kernelSize / 2, i = 0; y <= kernelSize / 2; y++) {
        for (int x = -kernelSize / 2; x <= kernelSize / 2; x++, i++) {
            weights[i] = exp(-((float)(x * x + y * y) / (2 * radius * radius)));
        }
    }
    //    LOG_INFO(TAG) << "Gaussian Kernel weights (" << weights.size() << "): " << std::endl;
    //    for (const auto& w : weights) {
    //        LOG_INFO(TAG) << std::setprecision(4) << std::scientific << w << ", ";
    //    }
    //    LOG_INFO(TAG) << std::endl;

    const int outWidth = kernelSize / 2 + 1;
    const int weightsCount = outWidth * outWidth;
    std::vector<std::array<float, 3>> weightsOut(weightsCount);
    KernelOptimizeBilinear2d(kernelSize, weights, &weightsOut);

    //    LOG_INFO(TAG) << "Bilinear Gaussian Kernel weights and offsets (" << weightsOut.size() << "): " << std::endl;
    //    for (const auto& [w, x, y] : weightsOut) {
    //        LOG_INFO(TAG) << w << " @ (" << x << " : " << y << "), " << std::endl;
    //    }

    return weightsOut;
}

void gaussianBlurSobelImage(gls::OpenCLContext* glsContext, const gls::cl_image_2d<gls::luma_pixel_float>& rawImage,
                            const gls::cl_image_2d<gls::rgba_pixel_float>& sobelImage,
                            std::array<float, 2> rawNoiseModel, float radius1, float radius2,
                            gls::cl_image_2d<gls::luma_alpha_pixel_float>* outputImage) {
    // Load the shader source
    const auto program = glsContext->loadProgram("demosaic");

    auto weightsOut1 = gaussianKernelBilinearWeights(radius1);
    auto weightsOut2 = gaussianKernelBilinearWeights(radius2);

    cl::Buffer weightsBuffer1(weightsOut1.begin(), weightsOut1.end(), /* readOnly */ true, /* useHostPtr */ false);
    cl::Buffer weightsBuffer2(weightsOut2.begin(), weightsOut2.end(), /* readOnly */ true, /* useHostPtr */ false);

    const auto linear_sampler = cl::Sampler(glsContext->clContext(), true, CL_ADDRESS_CLAMP_TO_EDGE, CL_FILTER_LINEAR);

    // Bind the kernel parameters
    auto kernel = cl::KernelFunctor<cl::Image2D,  // rawImage
                                    cl::Image2D,  // sobelImage
                                    int,          // samples1
                                    cl::Buffer,   // weights1
                                    int,          // samples2
                                    cl::Buffer,   // weights2
                                    cl_float2,    // rawVariance
                                    cl::Image2D,  // outputImage
                                    cl::Sampler   // linear_sampler
                                    >(program, "sampledConvolutionSobel");

    // Schedule the kernel on the GPU
    kernel(gls::OpenCLContext::buildEnqueueArgs(outputImage->width, outputImage->height), rawImage.getImage2D(),
           sobelImage.getImage2D(), (int)weightsOut1.size(), weightsBuffer1, (int)weightsOut2.size(), weightsBuffer2,
           cl_float2{rawNoiseModel[0], rawNoiseModel[1]}, outputImage->getImage2D(), linear_sampler);
}

void gaussianBlurImage(gls::OpenCLContext* glsContext, const gls::cl_image_2d<gls::rgba_pixel_float>& inputImage,
                       float radius, gls::cl_image_2d<gls::rgba_pixel_float>* outputImage) {
    // Load the shader source
    const auto program = glsContext->loadProgram("demosaic");

    const bool ordinary_gaussian = false;
    if (ordinary_gaussian) {
        // Bind the kernel parameters
        auto kernel = cl::KernelFunctor<cl::Image2D,  // inputImage
                                        float,        // radius
                                        cl::Image2D   // outputImage
                                        >(program, "gaussianBlurImage");

        // Schedule the kernel on the GPU
        kernel(gls::OpenCLContext::buildEnqueueArgs(outputImage->width, outputImage->height), inputImage.getImage2D(),
               radius, outputImage->getImage2D());
    } else {
        auto weightsOut = gaussianKernelBilinearWeights(radius);

        cl::Buffer weightsBuffer(weightsOut.begin(), weightsOut.end(), /* readOnly */ true, /* useHostPtr */ false);

        const auto linear_sampler =
            cl::Sampler(glsContext->clContext(), true, CL_ADDRESS_CLAMP_TO_EDGE, CL_FILTER_LINEAR);

        // Bind the kernel parameters
        auto kernel = cl::KernelFunctor<cl::Image2D,  // inputImage
                                        int,          // samples
                                        cl::Buffer,   // weights
                                        cl::Image2D,  // outputImage
                                        cl::Sampler   // linear_sampler
                                        >(program, "sampledConvolutionImage");

        // Schedule the kernel on the GPU
        kernel(gls::OpenCLContext::buildEnqueueArgs(outputImage->width, outputImage->height), inputImage.getImage2D(),
               (int)weightsOut.size(), weightsBuffer, outputImage->getImage2D(), linear_sampler);
    }
}

void blueNoiseImage(gls::OpenCLContext* glsContext, const gls::cl_image_2d<gls::rgba_pixel_float>& inputImage,
                    const gls::cl_image_2d<gls::luma_pixel_16>& blueNoiseImage, gls::Vector<2> lumaVariance,
                    gls::cl_image_2d<gls::rgba_pixel_float>* outputImage) {
    // Load the shader source
    const auto program = glsContext->loadProgram("demosaic");

    // Bind the kernel parameters
    auto kernel = cl::KernelFunctor<cl::Image2D,  // inputImage
                                    cl::Image2D,  // blueNoiseImage
                                    cl_float2,    // lumaVariance
                                    cl::Image2D,  // outputImage
                                    cl::Sampler   // linear_sampler
                                    >(program, "blueNoiseImage");

    const auto linear_sampler = cl::Sampler(glsContext->clContext(), true, CL_ADDRESS_REPEAT, CL_FILTER_LINEAR);

    // Schedule the kernel on the GPU
    kernel(gls::OpenCLContext::buildEnqueueArgs(outputImage->width, outputImage->height), inputImage.getImage2D(),
           blueNoiseImage.getImage2D(), {lumaVariance[0], lumaVariance[1]}, outputImage->getImage2D(), linear_sampler);
}

void blendHighlightsImage(gls::OpenCLContext* glsContext, const gls::cl_image_2d<gls::rgba_pixel_float>& inputImage,
                          float clip, gls::cl_image_2d<gls::rgba_pixel_float>* outputImage) {
    // Load the shader source
    const auto program = glsContext->loadProgram("demosaic");

    // Bind the kernel parameters
    auto kernel = cl::KernelFunctor<cl::Image2D,  // inputImage
                                    float,        // clip
                                    cl::Image2D   // outputImage
                                    >(program, "blendHighlightsImage");

    // Schedule the kernel on the GPU
    kernel(gls::OpenCLContext::buildEnqueueArgs(outputImage->width, outputImage->height), inputImage.getImage2D(), clip,
           outputImage->getImage2D());
}

YCbCrNLF MeasureYCbCrNLF(gls::OpenCLContext* glsContext, const gls::cl_image_2d<gls::rgba_pixel_float>& inputImage,
                         const gls::cl_image_2d<gls::luma_alpha_pixel_float>& sobelImage, float exposure_multiplier) {
    gls::cl_image_2d<gls::rgba_pixel_float> noiseStats(glsContext->clContext(), inputImage.width, inputImage.height);
    YCbCrNoiseStatistics(glsContext, inputImage, sobelImage, &noiseStats);
    // applyKernel(glsContext, "noiseStatistics_old", inputImage, &noiseStats);
    const auto noiseStatsCpu = noiseStats.mapImage();

    using double3 = gls::DVector<3>;

    // Only consider pixels with variance lower than the expected noise value
    double3 varianceMax = 0.001;

    // Limit to pixels the more linear intensity zone of the sensor
    const double maxValue = 0.5;
    const double minValue = 0.001;

    // Collect pixel statistics
    double s_x = 0;
    double s_xx = 0;
    double3 s_y = 0;
    double3 s_xy = 0;

    double N = 0;
    noiseStatsCpu.apply([&](const gls::rgba_pixel_float& ns, int x, int y) {
        double m = ns[0];
        double3 v = {ns[1], ns[2], ns[3]};

        bool validStats = !(std::isnan(m) || any(isnan(v)));

        if (validStats && m >= minValue && m <= maxValue && all(v <= varianceMax)) {
            s_x += m;
            s_y += v;
            s_xx += m * m;
            s_xy += m * v;
            N++;
        }
    });

    // Linear regression on pixel statistics to extract a linear noise model: nlf = A + B * Y
    auto nlfB = max((N * s_xy - s_x * s_y) / (N * s_xx - s_x * s_x), 1e-8);
    auto nlfA = max((s_y - nlfB * s_x) / N, 1e-8);

    // Estimate regression mean square error
    double3 err2 = 0;
    noiseStatsCpu.apply([&](const gls::rgba_pixel_float& ns, int x, int y) {
        double m = ns[0];
        double3 v = {ns[1], ns[2], ns[3]};

        bool validStats = !(std::isnan(m) || any(isnan(v)));

        if (validStats && m >= minValue && m <= maxValue && all(v <= varianceMax)) {
            auto nlfP = nlfA + nlfB * m;
            auto diff = nlfP - v;
            err2 += diff * diff;
        }
    });
    err2 /= N;

    //    LOG_INFO(TAG) << "1) Pyramid NLF A: " << std::setprecision(4) << std::scientific << nlfA << ", B: " << nlfB <<
    //    ", MSE: " << sqrt(err2)
    //              << " on " << std::setprecision(1) << std::fixed << 100 * N / (inputImage.width * inputImage.height)
    //              << "% pixels"<< std::endl;

    // Update the maximum variance with the model
    varianceMax = nlfB;

    // Redo the statistics collection limiting the sample to pixels that fit well the linear model
    s_x = 0;
    s_xx = 0;
    s_y = 0;
    s_xy = 0;
    N = 0;
    double3 newErr2 = 0;
    int discarded = 0;
    noiseStatsCpu.apply([&](const gls::rgba_pixel_float& ns, int x, int y) {
        double m = ns[0];
        double3 v = {ns[1], ns[2], ns[3]};

        bool validStats = !(std::isnan(m) || any(isnan(v)));

        if (validStats && m >= minValue && m <= maxValue && all(v <= varianceMax)) {
            auto nlfP = nlfA + nlfB * m;
            auto diff = abs(nlfP - v);
            auto diffSquare = diff * diff;

            if (all(diffSquare <= 0.5 * err2)) {
                s_x += m;
                s_y += v;
                s_xx += m * m;
                s_xy += m * v;
                N++;
                newErr2 += diffSquare;
            } else {
                discarded++;
            }
        }
    });
    newErr2 /= N;

    if (all(newErr2 <= err2)) {
        // Estimate the new regression parameters
        nlfB = max((N * s_xy - s_x * s_y) / (N * s_xx - s_x * s_x), 1e-8);
        nlfA = max((s_y - nlfB * s_x) / N, 1e-8);

        LOG_INFO(TAG) << "Pyramid NLF A: " << std::setprecision(4) << std::scientific << nlfA << ", B: " << nlfB
                      << ", MSE: " << sqrt(newErr2) << " on " << std::setprecision(1) << std::fixed
                      << 100 * N / (inputImage.width * inputImage.height) << "% pixels" << std::endl;
    } else {
        LOG_INFO(TAG) << "*** WARNING *** Pyramid NLF second iteration is worse: MSE: " << sqrt(newErr2) << " on "
                      << std::setprecision(1) << std::fixed << 100 * N / (inputImage.width * inputImage.height)
                      << "% pixels" << std::endl;
    }

    // assert(all(newErr2 < err2));

    noiseStats.unmapImage(noiseStatsCpu);

    double varianceExposureAdjustment = exposure_multiplier * exposure_multiplier;

    nlfA *= varianceExposureAdjustment;
    nlfB *= varianceExposureAdjustment;

    return std::pair(nlfA,  // A values
                     nlfB   // B values
    );
}

template <typename T>
struct sample {
    const T mean;
    const T var;
};

template <typename T>
struct line_model {
    T a;
    T b;
};

template <typename T>
struct ImageVectorPairAdapter {
    ImageVectorPairAdapter(const gls::image<T>& mean, const gls::image<T>& var) : _mean(mean), _var(var) {
        assert(mean.pixels().size() == var.pixels().size());
    }

    const gls::image<T>& _mean;
    const gls::image<T>& _var;

    const sample<T> operator[](size_t index) const { return sample<T>{_mean.pixels()[index], _var.pixels()[index]}; }

    size_t size() const { return _mean.pixels().size(); }
};

typedef ImageVectorPairAdapter<gls::rgba_pixel_float> RGBAImageVectorPairAdapter;

class RGBALineEstimator : virtual public RTL::Estimator<line_model<gls::Vector<4>>, sample<gls::rgba_pixel_float>,
                                                        RGBAImageVectorPairAdapter> {
   public:
    virtual line_model<gls::Vector<4>> ComputeModel(const RGBAImageVectorPairAdapter& data,
                                                    const std::set<int>& samples) {
        gls::Vector<4> s_x = 0;
        gls::Vector<4> s_y = 0;
        gls::Vector<4> s_xx = 0;
        gls::Vector<4> s_xy = 0;
        float N = 0;

        for (auto itr = samples.begin(); itr != samples.end(); itr++) {
            const sample<gls::rgba_pixel_float> p = data[*itr];

            gls::Vector<4> m = p.mean.v;
            gls::Vector<4> v = p.var.v;

            s_x += m;
            s_y += v;
            s_xx += m * m;
            s_xy += m * v;
            N++;
        }
        // Linear regression on pixel statistics to extract a linear noise model: nlf = A + B * Y
        auto nlfB = (N * s_xy - s_x * s_y) / (N * s_xx - s_x * s_x);
        auto nlfA = (s_y - nlfB * s_x) / N;

        return line_model<gls::Vector<4>>{nlfA, nlfB};
    }

    virtual float ComputeError(const line_model<gls::Vector<4>>& model, const sample<gls::rgba_pixel_float>& sample) {
        const auto diff = gls::Vector<4>(sample.var.v) - (model.a + model.b * gls::Vector<4>(sample.mean.v));

        return sqrt(dot(diff, diff));
    }
};

void dumpNoiseImage(const gls::image<gls::rgba_pixel_float>& image, float a, float b, const std::string& name) {
    gls::image<gls::luma_pixel_16> luma(image.size());

    luma.apply([&image, a, b](gls::luma_pixel_16* p, int x, int y) {
        *p = std::clamp((int)(0xffff * a * (image[y][x].green + b)), 0, 0xffff);
    });
    luma.write_png_file("/Users/fabio/Statistics/" + name + ".png");
}

// kernel void despeckleRawBlackImage(read_only image2d_t rawImage, int bayerPattern, write_only image2d_t
// despeckledImage);
void despeckleRawBlackImage(gls::OpenCLContext* glsContext, const gls::cl_image_2d<gls::luma_pixel_float>& rawImage,
                            BayerPattern bayerPattern, gls::cl_image_2d<gls::luma_pixel_float>* despeckledImage) {}

RawNLF MeasureRawNLF(gls::OpenCLContext* glsContext, const gls::cl_image_2d<gls::luma_pixel_float>& rawImage,
                     const gls::cl_image_2d<gls::rgba_pixel_float>& sobelImage, float exposure_multiplier,
                     BayerPattern bayerPattern) {
    gls::cl_image_2d<gls::rgba_pixel_float> meanImage(glsContext->clContext(), rawImage.width / 2, rawImage.height / 2);
    gls::cl_image_2d<gls::rgba_pixel_float> varImage(glsContext->clContext(), rawImage.width / 2, rawImage.height / 2);
    gls::cl_image_2d<gls::rgba_pixel_float> kurtImage(glsContext->clContext(), rawImage.width / 2, rawImage.height / 2);

    rawNoiseStatistics(glsContext, rawImage, bayerPattern, sobelImage, &meanImage, &varImage, &kurtImage);

    const auto meanImageCpu = meanImage.mapImage();
    const auto varImageCpu = varImage.mapImage();
    const auto kurtImageCpu = kurtImage.mapImage();

    //    static int count = 0;
    //    dumpNoiseImage(meanImageCpu, 1, 0, "mean9x9-" + std::to_string(count));
    //    dumpNoiseImage(varImageCpu, 1, 0, "variance9x9-" + std::to_string(count));
    //    dumpNoiseImage(kurtImageCpu, 0.1, 2, "kurtosis9x9-" + std::to_string(count));
    //    count++;

    const float minK = -1.0;
    const float maxK = 1.0;

    const bool use_ransac = false;
    if (use_ransac) {
        RGBALineEstimator estimator;
        RTL::LMedS<line_model<gls::Vector<4>>, sample<gls::rgba_pixel_float>, RGBAImageVectorPairAdapter> ransac(
            &estimator);
        ransac.SetParamThreshold(1e-6);
        ransac.SetParamIteration(100);

        const auto imageVectorPairAdapter = RGBAImageVectorPairAdapter(meanImageCpu, varImageCpu);

        line_model<gls::Vector<4>> model;
        float loss = ransac.FindBest(model, imageVectorPairAdapter, (int)imageVectorPairAdapter.size(), 2);

        model.a = max(model.a, 1e-8f);
        model.b = max(model.b, 1e-8f);

        LOG_INFO(TAG) << "Estimated line model a: " << std::setprecision(4) << std::scientific << model.a
                      << ", b: " << model.b << " with loss " << loss << std::endl;

        return std::pair(model.a,  // A values
                         model.b   // B values
        );
    }

    using double4 = gls::DVector<4>;

    // Only consider pixels with variance lower than the expected noise value
    double4 varianceMax = 0.001;

    // Limit to pixels the more linear intensity zone of the sensor
    const double maxValue = 0.5;
    const double minValue = 0.001;

    // Collect pixel statistics
    double4 s_x = 0;
    double4 s_y = 0;
    double4 s_xx = 0;
    double4 s_xy = 0;

    double N = 0;
    meanImageCpu.apply([&](const gls::rgba_pixel_float& mm, int x, int y) {
        double4 m = mm.v;
        double4 v = varImageCpu[y][x].v;
        double4 k = kurtImageCpu[y][x].v;

        bool validStats = !(any(isnan(m)) || any(isnan(v)) || any(isnan(k)));

        if (validStats && all(m >= double4(minValue)) && all(m <= double4(maxValue)) && all(v <= varianceMax) &&
            all(k > double4(minK)) && all(k < double4(maxK))) {
            s_x += m;
            s_y += v;
            s_xx += m * m;
            s_xy += m * v;
            N++;
        }
    });

    // Linear regression on pixel statistics to extract a linear noise model: nlf = A + B * Y
    auto nlfB = max((N * s_xy - s_x * s_y) / (N * s_xx - s_x * s_x), 1e-8);
    auto nlfA = max((s_y - nlfB * s_x) / N, 1e-8);

    // Estimate regression mean square error
    double4 err2 = 0;
    meanImageCpu.apply([&](const gls::rgba_pixel_float& mm, int x, int y) {
        double4 m = mm.v;
        double4 v = varImageCpu[y][x].v;
        double4 k = kurtImageCpu[y][x].v;

        bool validStats = !(any(isnan(m)) || any(isnan(v)) || any(isnan(k)));

        if (validStats && all(m >= double4(minValue)) && all(m <= double4(maxValue)) && all(v <= varianceMax) &&
            all(k > double4(minK)) && all(k < double4(maxK))) {
            auto nlfP = nlfA + nlfB * m;
            auto diff = nlfP - v;
            err2 += diff * diff;
        }
    });
    err2 /= N;

    LOG_INFO(TAG) << "RAW NLF A: " << std::setprecision(4) << std::scientific << nlfA << ", B: " << nlfB
                  << ", MSE: " << sqrt(err2) << " on " << std::setprecision(1) << std::fixed
                  << 100 * N / (rawImage.width * rawImage.height) << "% pixels" << std::endl;

    // Update the maximum variance with the model
    varianceMax = nlfB;

    // Redo the statistics collection limiting the sample to pixels that fit well the linear model
    s_x = 0;
    s_y = 0;
    s_xx = 0;
    s_xy = 0;
    N = 0;
    double4 newErr2 = 0;
    meanImageCpu.apply([&](const gls::rgba_pixel_float& mm, int x, int y) {
        double4 m = mm.v;
        double4 v = varImageCpu[y][x].v;
        double4 k = kurtImageCpu[y][x].v;

        bool validStats = !(any(isnan(m)) || any(isnan(v)) || any(isnan(k)));

        if (validStats && all(m >= double4(minValue)) && all(m <= double4(maxValue)) && all(v <= varianceMax) &&
            all(k > double4(minK)) && all(k < double4(maxK))) {
            const auto nlfP = nlfA + nlfB * m;
            const auto diff = abs(nlfP - v);
            const auto diffSquare = diff * diff;

            if (all(diffSquare <= 0.5 * err2)) {
                s_x += m;
                s_y += v;
                s_xx += m * m;
                s_xy += m * v;
                N++;
                newErr2 += diffSquare;
            }
        }
    });
    newErr2 /= N;

    // Estimate the new regression parameters
    nlfB = max((N * s_xy - s_x * s_y) / (N * s_xx - s_x * s_x), 1e-8);
    nlfA = max((s_y - nlfB * s_x) / N, 1e-8);

    assert(all(newErr2 < err2));

    LOG_INFO(TAG) << "RAW NLF A: " << std::setprecision(4) << std::scientific << nlfA << ", B: " << nlfB
                  << ", MSE: " << sqrt(newErr2) << " on " << std::setprecision(1) << std::fixed
                  << 100 * N / (rawImage.width * rawImage.height) << "% pixels" << std::endl;

    meanImage.unmapImage(meanImageCpu);
    varImage.unmapImage(varImageCpu);
    kurtImage.unmapImage(kurtImageCpu);

    double varianceExposureAdjustment = exposure_multiplier * exposure_multiplier;

    nlfA *= varianceExposureAdjustment;
    nlfB *= varianceExposureAdjustment;

    return std::pair(nlfA,  // A values
                     nlfB   // B values
    );
}

void clFuseFrames(gls::OpenCLContext* glsContext, const gls::cl_image_2d<gls::rgba_pixel_float>& referenceImage,
                  const gls::cl_image_2d<gls::luma_alpha_pixel_float>& gradientImage,
                  const gls::cl_image_2d<gls::rgba_pixel_float>& inputImage,
                  const gls::cl_image_2d<gls::rgba_pixel_float>& previousFusedImage,
                  const gls::Matrix<3, 3>& homography, const gls::Vector<3>& var_a, const gls::Vector<3>& var_b,
                  int fusedFrames, gls::cl_image_2d<gls::rgba_pixel_float>* newFusedImage) {
    // Load the shader source
    const auto program = glsContext->loadProgram("demosaic");

    // Bind the kernel parameters
    auto kernel = cl::KernelFunctor<cl::Image2D,        // referenceImage
                                    cl::Image2D,        // gradientImage
                                    cl::Image2D,        // inputImage
                                    cl::Image2D,        // inputFusedImage
                                    gls::Matrix<3, 3>,  // homography
                                    cl::Sampler,        // linear_sampler
                                    cl_float3,          // var_a
                                    cl_float3,          // var_b
                                    int,                // fusedFrames
                                    cl::Image2D         // outputFusedImage
                                    >(program, "fuseFrames");

    cl_float3 cl_var_a = {var_a[0], var_a[1], var_a[2]};
    cl_float3 cl_var_b = {var_b[0], var_b[1], var_b[2]};

    const auto linear_sampler = cl::Sampler(glsContext->clContext(), true, CL_ADDRESS_CLAMP_TO_EDGE, CL_FILTER_LINEAR);

    // Schedule the kernel on the GPU
    kernel(gls::OpenCLContext::buildEnqueueArgs(newFusedImage->width, newFusedImage->height),
           referenceImage.getImage2D(), gradientImage.getImage2D(), inputImage.getImage2D(),
           previousFusedImage.getImage2D(), homography, linear_sampler, cl_var_a, cl_var_b, fusedFrames,
           newFusedImage->getImage2D());
}

template <typename T>
void subtractNoiseFusedImage(gls::OpenCLContext* glsContext, const gls::cl_image_2d<T>& inputImage,
                             const gls::cl_image_2d<T>& inputImage1, const gls::cl_image_2d<T>& inputImageDenoised1,
                             gls::cl_image_2d<T>* outputImage) {
    // Load the shader source
    const auto program = glsContext->loadProgram("demosaic");

    const auto linear_sampler = cl::Sampler(glsContext->clContext(), true, CL_ADDRESS_CLAMP_TO_EDGE, CL_FILTER_LINEAR);

    // Bind the kernel parameters
    auto kernel = cl::KernelFunctor<cl::Image2D,  // inputImage
                                    cl::Image2D,  // inputImage1
                                    cl::Image2D,  // inputImageDenoised1
                                    cl::Image2D,  // outputImage
                                    cl::Sampler   // linear_sampler
                                    >(program, "subtractNoiseFusedImage");

    // Schedule the kernel on the GPU
    kernel(gls::OpenCLContext::buildEnqueueArgs(outputImage->width, outputImage->height), inputImage.getImage2D(),
           inputImage1.getImage2D(), inputImageDenoised1.getImage2D(), outputImage->getImage2D(), linear_sampler);
}

template void subtractNoiseFusedImage(gls::OpenCLContext* glsContext,
                                      const gls::cl_image_2d<gls::rgba_pixel_float>& inputImage,
                                      const gls::cl_image_2d<gls::rgba_pixel_float>& inputImage1,
                                      const gls::cl_image_2d<gls::rgba_pixel_float>& inputImageDenoised1,
                                      gls::cl_image_2d<gls::rgba_pixel_float>* outputImage);

template <typename T>
void clRescaleImage(gls::OpenCLContext* cLContext, const gls::cl_image_2d<T>& inputImage,
                    gls::cl_image_2d<T>* outputImage) {
    // Load the shader source
    const auto program = cLContext->loadProgram("demosaic");

    // Bind the kernel parameters
    auto kernel = cl::KernelFunctor<cl::Image2D,  // inputImage
                                    cl::Image2D,  // outputImage
                                    cl::Sampler   // linear_sampler
                                    >(program, "rescaleImage");

    const auto linear_sampler = cl::Sampler(cLContext->clContext(), true, CL_ADDRESS_CLAMP_TO_EDGE, CL_FILTER_LINEAR);

    kernel(
#if __APPLE__
        gls::OpenCLContext::buildEnqueueArgs(outputImage->width, outputImage->height),
#else
        cl::EnqueueArgs(cl::NDRange(outputImage->width, outputImage->height), cl::NDRange(32, 32)),
#endif
        inputImage.getImage2D(), outputImage->getImage2D(), linear_sampler);
}

template void clRescaleImage(gls::OpenCLContext* cLContext, const gls::cl_image_2d<gls::rgba_pixel>& inputImage,
                             gls::cl_image_2d<gls::rgba_pixel>* outputImage);

template void clRescaleImage(gls::OpenCLContext* cLContext, const gls::cl_image_2d<gls::rgba_pixel_float>& inputImage,
                             gls::cl_image_2d<gls::rgba_pixel_float>* outputImage);

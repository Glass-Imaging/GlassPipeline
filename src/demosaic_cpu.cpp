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

#include "demosaic.hpp"

enum { red = 0, green = 1, blue = 2, green2 = 3 };

void interpolateGreen(const gls::image<gls::luma_pixel_16>& rawImage,
                      gls::image<gls::rgb_pixel_16>* rgbImage, BayerPattern bayerPattern) {
    const int width = rawImage.width;
    const int height = rawImage.height;

    auto offsets = bayerOffsets[bayerPattern];
    const gls::point r = offsets[red];
    const gls::point g = offsets[green];

    // copy RAW data to RGB layer and remove hot pixels
    for (int y = 0; y < height; y++) {
        int color = (y & 1) == (r.y & 1) ? red : blue;
        int x0 = (y & 1) == (g.y & 1) ? g.x + 1 : g.x;
        for (int x = 0; x < width; x++) {
            bool colorPixel = (x & 1) == (x0 & 1);
            int channel = colorPixel ? color : green;

            int value = rawImage[y][x];
            if (x >= 2 && x < width - 2 && y >= 2 && y < height - 2) {
                int v[12];
                int n;
                if (!colorPixel) {
                    n = 8;
                    v[0] = rawImage[y - 1][x - 1];
                    v[1] = rawImage[y - 1][x + 1];
                    v[2] = rawImage[y + 1][x - 1];
                    v[3] = rawImage[y + 1][x + 1];

                    v[4] = 2 * rawImage[y - 1][x];
                    v[5] = 2 * rawImage[y + 1][x];
                    v[6] = 2 * rawImage[y][x + 1];
                    v[7] = 2 * rawImage[y][x + 1];
                } else {
                    n = 12;
                    v[0] = rawImage[y - 2][x];
                    v[1] = rawImage[y + 2][x];
                    v[2] = rawImage[y][x - 2];
                    v[3] = rawImage[y][x + 2];

                    v[4] = 2 * rawImage[y - 1][x - 1];
                    v[5] = 2 * rawImage[y - 1][x + 1];
                    v[6] = 2 * rawImage[y + 1][x - 1];
                    v[7] = 2 * rawImage[y + 1][x + 1];

                    v[8] = 2 * rawImage[y - 1][x];
                    v[9] = 2 * rawImage[y + 1][x];
                    v[10] = 2 * rawImage[y][x - 1];
                    v[11] = 2 * rawImage[y][x + 1];
                };
                bool replace = true;
                for (int i = 0; i < n; i++)
                    if (value < 2 * v[i]) {
                        replace = false;
                        break;
                    }
                if (replace) value = (v[0] + v[1] + v[2] + v[3]) / 4;
            }
            (*rgbImage)[y][x][channel] = value;
        }
    }

    // green channel interpolation

    for (int y = 2; y < height - 2; y++) {
        int color = (y & 1) == (r.y & 1) ? red : blue;
        int x0 = (y & 1) == (g.y & 1) ? g.x + 1 : g.x;

        int g_left  = (*rgbImage)[y][x0 - 1][green];
        int c_xy    = (*rgbImage)[y][x0][color];
        int c_left  = (*rgbImage)[y][x0 - 2][color];

        for (int x = x0 + 2; x < width - 2; x += 2) {
            int g_right = (*rgbImage)[y][x + 1][green];
            int g_up    = (*rgbImage)[y - 1][x][green];
            int g_down  = (*rgbImage)[y + 1][x][green];
            int g_dh    = abs(g_left - g_right);
            int g_dv    = abs(g_up - g_down);

            int c_right = (*rgbImage)[y][x + 2][color];
            int c_up    = (*rgbImage)[y - 2][x][color];
            int c_down  = (*rgbImage)[y + 2][x][color];
            int c_dh    = abs(c_left + c_right - 2 * c_xy);
            int c_dv    = abs(c_up + c_down - 2 * c_xy);

            // Minimum derivative value for edge directed interpolation (avoid aliasing)
            int dThreshold = 1200;

            // we're doing edge directed bilinear interpolation on the green channel,
            // which is a low pass operation (averaging), so we add some signal from the
            // high frequencies of the observed color channel

            int sample;
            if (g_dv + c_dv > dThreshold && g_dv + c_dv > g_dh + c_dh) {
                sample = (g_left + g_right) / 2;
                if (sample < 4 * c_xy && c_xy < 4 * sample) {
                    sample += (c_xy - (c_left + c_right) / 2) / 4;
                }
            } else if (g_dh + c_dh > dThreshold && g_dh + c_dh > g_dv + c_dv) {
                sample = (g_up + g_down) / 2;
                if (sample < 4 * c_xy && c_xy < 4 * sample) {
                    sample += (c_xy - (c_up + c_down) / 2) / 4;
                }
            } else {
                sample = (g_up + g_left + g_down + g_right) / 4;
                if (sample < 4 * c_xy && c_xy < 4 * sample) {
                    sample += (c_xy - (c_left + c_right + c_up + c_down) / 4) / 8;
                }
            }

            (*rgbImage)[y][x][green] = clamp_uint16(sample);
            g_left = g_right;
            c_left = c_xy;
            c_xy   = c_right;
        }
    }
}

void interpolateRedBlue(gls::image<gls::rgb_pixel_16>* image, BayerPattern bayerPattern) {
    const int width = image->width;
    const int height = image->height;

    auto offsets = bayerOffsets[bayerPattern];

    for (int y = 1; y < height - 1; y++) {
        for (int x = 1; x < width - 1; x++) {
            for (int color : {red, blue}) {
                const gls::point c = offsets[color];

                if (((x + c.x) & 1) != (c.x & 1) || ((y + c.y) & 1) != (c.y & 1)) {
                    int sample;
                    int cg = (*image)[y + c.y][x + c.x][green];

                    if (((x + c.x) & 1) != (c.x & 1) && ((y + c.y) & 1) != (c.y & 1)) {
                        // Pixel at color location
                        int g_ne = (*image)[y + c.y + 1][x + c.x - 1][green];
                        int g_nw = (*image)[y + c.y + 1][x + c.x + 1][green];
                        int g_sw = (*image)[y + c.y - 1][x + c.x + 1][green];
                        int g_se = (*image)[y + c.y - 1][x + c.x - 1][green];

                        int c_ne = g_ne - (*image)[y + c.y + 1][x + c.x - 1][color];
                        int c_nw = g_nw - (*image)[y + c.y + 1][x + c.x + 1][color];
                        int c_sw = g_sw - (*image)[y + c.y - 1][x + c.x + 1][color];
                        int c_se = g_se - (*image)[y + c.y - 1][x + c.x - 1][color];

                        int d_ne_sw = abs(c_ne - c_sw);
                        int d_nw_se = abs(c_nw - c_se);

                        // Minimum gradient for edge directed interpolation
                        int dThreshold = 800;
                        if (d_ne_sw > dThreshold && d_ne_sw > d_nw_se) {
                            sample = cg - (c_nw + c_se) / 2;
                        } else if (d_nw_se > dThreshold && d_nw_se > d_ne_sw) {
                            sample = cg - (c_ne + c_sw) / 2;
                        } else {
                            sample = cg - (c_ne + c_sw + c_nw + c_se) / 4;
                        }
                    } else if (((x + c.x) & 1) == (c.x & 1) && ((y + c.y) & 1) != (c.y & 1)) {
                        // Pixel at green location - vertical
                        int g_up    = (*image)[y + c.y - 1][x + c.x][green];
                        int g_down  = (*image)[y + c.y + 1][x + c.x][green];

                        int c_up    = g_up - (*image)[y + c.y - 1][x + c.x][color];
                        int c_down  = g_down - (*image)[y + c.y + 1][x + c.x][color];

                        sample = cg - (c_up + c_down) / 2;
                    } else {
                        // Pixel at green location - horizontal
                        int g_left  = (*image)[y + c.y][x + c.x - 1][green];
                        int g_right = (*image)[y + c.y][x + c.x + 1][green];

                        int c_left  = g_left - (*image)[y + c.y][x + c.x - 1][color];
                        int c_right = g_right - (*image)[y + c.y][x + c.x + 1][color];

                        sample = cg - (c_left + c_right) / 2;
                    }

                    (*image)[y + c.y][x + c.x][color] = clamp_uint16(sample);
                }
            }
        }
    }
}

gls::image<gls::rgb_pixel_16>::unique_ptr demosaicImageCPU(const gls::image<gls::luma_pixel_16>& rawImage,
                                                           gls::tiff_metadata* metadata, bool auto_white_balance) {
    DemosaicParameters demosaicParameters;
    unpackDNGMetadata(rawImage, metadata, &demosaicParameters, auto_white_balance, nullptr, false);

    printf("Begin demosaicing image (CPU)...\n");

    const auto offsets = bayerOffsets[demosaicParameters.bayerPattern];
    gls::image<gls::luma_pixel_16> scaledRawImage = gls::image<gls::luma_pixel_16>(rawImage.width, rawImage.height);
    for (int y = 0; y < rawImage.height / 2; y++) {
        for (int x = 0; x < rawImage.width / 2; x++) {
            for (int c = 0; c < 4; c++) {
                const auto& o = offsets[c];
                scaledRawImage[2 * y + o.y][2 * x + o.x] = clamp_uint16(demosaicParameters.scale_mul[c] * (rawImage[2 * y + o.y][2 * x + o.x] - demosaicParameters.black_level));
            }
        }
    }

    auto rgbImage = std::make_unique<gls::image<gls::rgb_pixel_16>>(rawImage.width, rawImage.height);

    printf("interpolating green channel...\n");
    interpolateGreen(scaledRawImage, rgbImage.get(), demosaicParameters.bayerPattern);

    printf("interpolating red and blue channels...\n");
    interpolateRedBlue(rgbImage.get(), demosaicParameters.bayerPattern);

    // Transform to RGB space
    for (int y = 0; y < rgbImage->height; y++) {
        for (int x = 0; x < rgbImage->width; x++) {
            auto& p = (*rgbImage)[y][x];
            const auto op = demosaicParameters.rgb_cam * /* mCamMul * */ gls::Vector<3>({ (float) p[0], (float) p[1], (float) p[2] });
            p = { clamp_uint16(op[0]), clamp_uint16(op[1]), clamp_uint16(op[2]) };
        }
    }

    return rgbImage;
}

//
//  PCA.hpp
//  GlassPipelineLib
//
//  Created by Fabio Riccardi on 3/1/23.
//

#ifndef PCA_hpp
#define PCA_hpp

#include "gls_image.hpp"

void pca(const gls::image<gls::rgba_pixel_float>& input, int patch_size, gls::image<std::array<gls::float16_t, 8>>* pca_image);

#endif /* PCA_hpp */

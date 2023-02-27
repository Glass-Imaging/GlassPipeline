cmake_minimum_required(VERSION 3.16) 

project("glsPipeline")

set(CMAKE_CXX_STANDARD 20)
set(ROOT_DIR ${CMAKE_SOURCE_DIR}/../)

# CL will throw an unknown error on Linux if it is not explicitly linked with pthread, see flags at the bottom
find_package(Threads REQUIRED)

add_library( libz SHARED IMPORTED )
set_target_properties( libz PROPERTIES IMPORTED_LOCATION /usr/lib/x86_64-linux-gnu/libz.so )
add_library( libpng SHARED IMPORTED )
set_target_properties( libpng PROPERTIES IMPORTED_LOCATION /usr/lib/x86_64-linux-gnu/libpng.so )
add_library( libjpeg SHARED IMPORTED )
set_target_properties( libjpeg PROPERTIES IMPORTED_LOCATION /usr/lib/x86_64-linux-gnu/libjpeg.so ) #TODO: Possibly use jpeg-turbo
add_library( libtiff SHARED IMPORTED )
set_target_properties( libtiff PROPERTIES IMPORTED_LOCATION /usr/lib/x86_64-linux-gnu/libtiff.so )
add_library( libtiffxx SHARED IMPORTED )
set_target_properties( libtiffxx PROPERTIES IMPORTED_LOCATION /usr/lib/x86_64-linux-gnu/libtiffxx.so )

add_library( glsImage SHARED IMPORTED )
set_target_properties( glsImage PROPERTIES IMPORTED_LOCATION ${ROOT_DIR}/GlassImage/linux/build/libglsImage.so )

add_library(
    glsPipeline
    SHARED

    ${ROOT_DIR}/src/demosaic_cl.cpp
    ${ROOT_DIR}/src/demosaic.cpp
    ${ROOT_DIR}/src/demosaic_cpu.cpp
    ${ROOT_DIR}/src/demosaic_utils.cpp
    ${ROOT_DIR}/src/homography.cpp
    ${ROOT_DIR}/src/pyramid_processor.cpp
    ${ROOT_DIR}/src/RANSAC.cpp
    ${ROOT_DIR}/src/raw_converter.cpp
    ${ROOT_DIR}/src/SURF.cpp
    ${ROOT_DIR}/src/ThreadPool.cpp

    ${ROOT_DIR}/src/CanonEOSRPCalibration.cpp
    ${ROOT_DIR}/src/IMX571Calibration.cpp
    ${ROOT_DIR}/src/iPhone11Calibration.cpp
    ${ROOT_DIR}/src/LeicaQ2Calibration.cpp
    ${ROOT_DIR}/src/RicohGRIIICalibration.cpp
    ${ROOT_DIR}/src/Sonya6400Calibration.cpp
    ${ROOT_DIR}/src/CameraCalibration.cpp
)

target_include_directories( glsPipeline PRIVATE ${ROOT_DIR}/GlassImage/include ${ROOT_DIR}/include )

target_link_libraries(
    glsPipeline
    glsImage
    libjpeg
    libpng
    libtiff
    libtiffxx
    libz
    OpenCL
)

###
### build imagingPipeline
###

# Copy OpenCL kernels
file(COPY ${ROOT_DIR}/src/OpenCL/demosaic.cl DESTINATION ${ROOT_DIR}/linux/build/OpenCL)
file(COPY ${ROOT_DIR}/src/OpenCL/SURF.cl DESTINATION ${ROOT_DIR}/linux/build/OpenCL)

# Copy asset files
file(COPY ${ROOT_DIR}/Assets/HDR_L_0b.png DESTINATION ${ROOT_DIR}/linux/build/Assets)
file(COPY ${ROOT_DIR}/Assets/HDR_L_0.png DESTINATION ${ROOT_DIR}/linux/build/Assets)

add_executable(
    imagingPipeline
    ${ROOT_DIR}/ImagingPipeline/imagingPipeline.cpp
)

target_include_directories( imagingPipeline PRIVATE ${ROOT_DIR}/GlassImage/include ${ROOT_DIR}/include )

target_link_libraries(
    imagingPipeline
    glsPipeline    
)

# Setting pthread flags to prevent silent OpenCL error, works for g++ and Clang
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -Werror=return-type")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pthread")

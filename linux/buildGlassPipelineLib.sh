# /bin/sh

# start fresh
rm -fr build

# build glsImage
cd ../GlassImage/linux
rm -fr build
./buildGlassImageLib.sh
cd -

mkdir -p build
cd build

cmake -D CMAKE_C_COMPILER=clang -D CMAKE_CXX_COMPILER=clang++ -std=c++20 -O3 ..

# build the library
make glsPipeline

# build the test app
make imagingPipeline

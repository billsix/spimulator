EXAMPLE_DIR=$(pwd)

# build MUSL

cd deps/musl/
mkdir build
mkdir buildInstall

MUSL_DIR=$(pwd)

cd build
CFLAGS="-g -O0" ../configure --disable-shared --prefix=$MUSL_DIR/buildInstall/ && make && make install
export PATH=$MUSL_DIR/buildInstall/bin:$PATH

cd $EXAMPLE_DIR



mkdir build
mkdir buildInstall
cd build

export CC="musl-gcc -static "

cmake -DCMAKE_INSTALL_PREFIX=../buildInstall  -DCMAKE_BUILD_TYPE=Debug -G "CodeBlocks - Unix Makefiles" ../
cmake --build  . --target all
cmake --build  . --target install

codeblocks build/examples.cbp

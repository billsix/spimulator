mkdir build
mkdir buildInstall
cd build

cmake -DCMAKE_INSTALL_PREFIX=../buildInstall  -DCMAKE_BUILD_TYPE=Debug ../
cmake --build  . --target all
cmake --build  . --target test || exit 1
cmake --build  . --target install

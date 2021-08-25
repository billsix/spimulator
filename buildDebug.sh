if [ "$(uname)" == "Darwin" ]; then
    # Do something under Mac OS X platform
    echo
elif [ "$(expr substr $(uname -s) 1 5)" == "Linux" ]; then
    cd CPU/
    flex -I -8 -o lex.yy.cpp scanner.l
    bison -d --defines=parser_yacc.h --output=parser_yacc.cpp -p yy parser.y
    bison -d --defines=../QtSpim/parser_yacc.h --output=../QtSpim/parser_yacc.cpp -p yy parser.y
    cd ../
elif [ "$(expr substr $(uname -s) 1 10)" == "MINGW32_NT" ]; then
    # Do something under 32 bits Windows NT platform
    echo
elif [ "$(expr substr $(uname -s) 1 10)" == "MINGW64_NT" ]; then
    # Do something under 64 bits Windows NT platform
    echo
fi


mkdir build
mkdir buildInstall
cd build

cmake -DCMAKE_INSTALL_PREFIX=../buildInstall  -DCMAKE_BUILD_TYPE=Debug ../
cmake --build  . --target all
cmake --build  . --target install
#cd ../buildInstall
#./bin/spim

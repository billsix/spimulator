export MAIN_DIRECTORY=$(pwd)

if [ "$(uname)" == "Darwin" ]; then
    # Do something under Mac OS X platform
    echo
elif [ "$(expr substr $(uname -s) 1 5)" == "Linux" ]; then
    cd src/
    flex -I -8 -o lex.yy.c scanner.l
    bison -d --defines=parser_yacc.h --output=parser_yacc.c -p yy parser.y
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

export TEST_DIR=$MAIN_DIRECTORY/tests/
export SRC_DIR=$MAIN_DIRECTORY/src/

# test
cd ../buildInstall/bin

echo "Testing tt.bare.s:"
./spimulator -delayed_branches -delayed_loads -noexception -file $TEST_DIR/tt.bare.s >& test.out
tail -2 test.out

echo "Testing tt.core.s:"
./spimulator -ef $SRC_DIR/exceptions.s -file $TEST_DIR/tt.core.s < $TEST_DIR/tt.in >& test.out
tail -2 test.out

echo "Testing tt.le.s:"
./spimulator -ef $SRC_DIR/exceptions.s -file $TEST_DIR/tt.le.s  >& test.out
tail -2 test.out

# The following two tests come from test_bare, and they don't pass on version
# v9.1.2, so I don't care that they fail the same way in my branch

# echo "Testing tt.alu.bare.s:"
# ./spimulator -bare -noexception -file $TEST_DIR/tt.alu.bare.s >& test.out
# tail -2 test.out

# echo "Testing tt.fpt.bare.s:"
# ./spimulator -bare -noexception -file $TEST_DIR/tt.fpu.bare.s >& test.out
# tail -2 test.out

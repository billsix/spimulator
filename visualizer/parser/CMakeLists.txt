cmake_minimum_required(VERSION 3.0)

if(WIN32)
project(c-parser LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 98)
file(GLOB_RECURSE CFILES "${CMAKE_SOURCE_DIR}/src/*.c")
SET_SOURCE_FILES_PROPERTIES(${CFILES} PROPERTIES LANGUAGE CXX )
else(WIN32)
project(c-parser LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
endif(WIN32)


set(SOURCE_FILES
  src/foo.c
)




add_library(
  c-parser-lib
  ${SOURCE_FILES}
  )


add_executable(cpp_test  test/test.cpp)

target_link_libraries(cpp_test c-parser-lib)


enable_testing()
add_test(
  NAME cpp_test
  COMMAND $<TARGET_FILE:cpp_test> --success
  )

include_directories(
  src/
  deps/Catch2/
  )


install(TARGETS c-parser-lib DESTINATION lib)

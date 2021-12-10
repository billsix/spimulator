#include <iostream>

#define CATCH_CONFIG_MAIN
#include "catch2/catch.hpp"


// #ifdef __cplusplus
// #define C_DECL_BEGIN extern "C" {
// #define C_DECL_END }
// #else
// #define C_DECL_BEGIN
// #define C_DECL_END
// #endif

// C_DECL_BEGIN

extern "C" {
int fact(int n);
}

TEST_CASE( "fact 0 ", "[short]" ) {
  int result = fact(0);
  std::cout << result << std::endl;
  REQUIRE( 1 == result );
}

TEST_CASE( "fact 1", "[short]" ) {
  int result = fact(1);
  REQUIRE( 1 == result );
}

TEST_CASE( "fact 2", "[short]" ) {
  int result = fact(2);
  REQUIRE( 2 == result );
}

TEST_CASE( "fact 3", "[short]" ) {
  int result = fact(3);
  REQUIRE( 6 == result );
}

cmake_minimum_required(VERSION 3.0)

project(spimulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 98)

add_compile_definitions(IMGUI_IMPL_OPENGL_LOADER_GL3W=1)


set(DEFAULT_EXCEPTION_HANDLER "${CMAKE_INSTALL_PREFIX}/share/spim/exceptions.s")

configure_file(src/config.h.in src/config.h)

# set SOURCE_FILES to all of the c files
set(INCLUDE_FILES
  src/data.h
  src/inst.h
  src/mem.h
  src/op.h
  src/parser.h
  src/reg.h
  src/run.h
  src/scanner.h
  src/spim.h
  src/spim-utils.h
  src/string-stream.h
  src/sym-tbl.h
  src/syscall.h
  src/parser_yacc.h
)


set(SOURCE_FILES
  src/spim-utils.cpp
  src/lex.yy.cpp
  src/parser_yacc.cpp
  src/run.cpp
  src/mem.cpp
  src/inst.cpp
  src/data.cpp
  src/sym-tbl.cpp
  src/syscall.cpp
  src/display-utils.cpp
  src/string-stream.cpp
)

set(SPIM_SOURCE_FILES
  ${SOURCE_FILES}
  src/spim.cpp
)




# definition of the spim, and its dependent sources
set(SPIM_EXE
  spimulator)
add_executable(
  ${SPIM_EXE}
  ${SPIM_SOURCE_FILES}
  ${INCLUDE_FILES}
  )


include_directories(
  src/
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}/src/
  )

if(WIN32)
  set(LINK_LIBRARIES wsock32 ws2_32)
else()
  set(LINK_LIBRARIES m)
endif()

target_link_libraries(${SPIM_EXE} ${LINK_LIBRARIES} )

install(TARGETS ${SPIM_EXE} DESTINATION bin)
install(FILES src/exceptions.s DESTINATION share/spim/)

cmake_minimum_required(VERSION 3.0)

if(WIN32)
project(spimulator LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 98)
file(GLOB_RECURSE CFILES "${CMAKE_SOURCE_DIR}/src/*.c")
SET_SOURCE_FILES_PROPERTIES(${CFILES} PROPERTIES LANGUAGE CXX )
else(WIN32)
project(spimulator LANGUAGES C)
set(CMAKE_C_STANDARD 11)
endif(WIN32)

add_compile_definitions(IMGUI_IMPL_OPENGL_LOADER_GL3W=1)


set(DEFAULT_EXCEPTION_HANDLER "${CMAKE_INSTALL_PREFIX}/share/spimulator/exceptions.s")

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
  src/spim-utils.c
  src/lex.yy.c
  src/parser_yacc.c
  src/run.c
  src/mem.c
  src/inst.c
  src/data.c
  src/sym-tbl.c
  src/syscall.c
  src/display-utils.c
  src/string-stream.c
)

set(SPIM_SOURCE_FILES
  ${SOURCE_FILES}
  src/spim.c
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
install(FILES src/exceptions.s DESTINATION share/spimulator/)

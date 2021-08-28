cmake_minimum_required(VERSION 3.0)

project(spim LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)

add_compile_definitions(IMGUI_IMPL_OPENGL_LOADER_GL3W=1)


set(DEFAULT_EXCEPTION_HANDLER "${CMAKE_INSTALL_PREFIX}/share/spim/exceptions.s")

configure_file(config.h.in config.h)

# set SOURCE_FILES to all of the c files
set(INCLUDE_FILES
  CPU/data.h
  CPU/inst.h
  CPU/mem.h
  CPU/op.h
  CPU/parser.h
  CPU/reg.h
  CPU/run.h
  CPU/scanner.h
  CPU/spim.h
  CPU/spim-utils.h
  CPU/string-stream.h
  CPU/sym-tbl.h
  CPU/syscall.h
  CPU/parser_yacc.h
)


set(SOURCE_FILES
  CPU/spim-utils.cpp
  CPU/lex.yy.cpp
  CPU/parser_yacc.cpp
  CPU/run.cpp
  CPU/mem.cpp
  CPU/inst.cpp
  CPU/data.cpp
  CPU/sym-tbl.cpp
  CPU/syscall.cpp
  CPU/display-utils.cpp
  CPU/string-stream.cpp
)

set(SPIM_SOURCE_FILES
  ${SOURCE_FILES}
  spim/spim.cpp
)




# definition of the spim, and its dependent sources
set(SPIM_EXE
  spim)
add_executable(
  ${SPIM_EXE}
  ${SPIM_SOURCE_FILES}
  ${INCLUDE_FILES}
  )


include_directories(
  CPU/
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}/spim
  )

if(WIN32)
  target_link_libraries(${SPIM_EXE} wsock32 ws2_32)
endif()


install(TARGETS ${SPIM_EXE} DESTINATION bin)
install(FILES CPU/exceptions.s DESTINATION share/spim/)

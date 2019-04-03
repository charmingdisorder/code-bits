cmake_minimum_required(VERSION 2.8)

project(slkq)

set(CMAKE_C_FLAGS "-fPIC -ggdb -Wall")
set(CMAKE_C_FLAGS_RELEASE "-Os")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -Wstrict-prototypes -Wall -Wundef -Wsign-compare -Wcomment -pedantic -finline-functions -fno-strict-aliasing -g")

set(KERNEL_SRC ${CMAKE_SOURCE_DIR}/kernel/slkq.c ${CMAKE_SOURCE_DIR}/common/slkq.h)
set(KERNEL_MODULE kernel/slkq.ko)

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/kernel)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/common)

add_custom_command(
  OUTPUT ${KERNEL_MODULE}
  COMMAND cp ${CMAKE_SOURCE_DIR}/kernel/slkq.c ${CMAKE_BINARY_DIR}/kernel
  COMMAND cp ${CMAKE_SOURCE_DIR}/common/slkq.h ${CMAKE_BINARY_DIR}/common
  COMMAND echo "obj-m += slkq.o" > kernel/Kbuild
  COMMAND make W=1 -C /lib/modules/`uname -r`/build M=${CMAKE_BINARY_DIR}/kernel
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  DEPENDS ${KERNEL_SRC}
  )

add_custom_target(
  kernel ALL
  DEPENDS ${KERNEL_MODULE}
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  )

add_executable(slkq_reader user/slkq_reader.c user/log.c)
add_executable(slkq_write user/slkq_write.c)

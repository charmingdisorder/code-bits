cmake_minimum_required(VERSION 2.8)

project(brctl_netlink)

set(CMAKE_C_FLAGS "-fPIC -ggdb -Wall")
set(CMAKE_C_FLAGS_RELEASE "-Os")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -Wstrict-prototypes -Wall -Wundef -Wsign-compare -Wcomment -pedantic -finline-functions -fno-strict-aliasing -g")

find_library(LIBNL nl-3)
find_path(LIBNL_INCLUDE_DIR NAMES libnl3/netlink/socket.h)
include_directories(${LIBNL_INCLUDE_DIR})
include_directories(${LIBNL_INCLUDE_DIR}/libnl3)
link_libraries(${LIBNL})

find_library(LIBNL-ROUTE nl-route-3 libnl-route-3.a nl-route-3)
link_libraries(${LIBNL-ROUTE})

add_executable(brctl_netlink src/brctl_netlink.c)

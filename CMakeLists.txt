cmake_minimum_required(VERSION 3.20)
project(BGP_Simulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -mtune=native -ffast-math -funroll-loops -DNDEBUG -flto")

find_package(BZip2 REQUIRED)

set(SOURCES
    src/simulator.cpp
    src/as_graph.cpp
    src/bgp.cpp
)

add_executable(bgp_sim ${SOURCES})
target_include_directories(bgp_sim PRIVATE src)
target_link_libraries(bgp_sim PRIVATE BZip2::BZip2)


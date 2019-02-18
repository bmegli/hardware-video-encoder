cmake_minimum_required(VERSION 3.0)

project(
    hve
)

# altenatively SHARED instead of STATIC for a shared library
add_library(hve STATIC hve.c)
target_link_libraries(hve avcodec avutil)
install(TARGETS hve DESTINATION lib)
install(FILES hve.h DESTINATION include)

add_executable(hve-encode-raw-h264 examples/hve_encode_raw_h264.c)
target_link_libraries(hve-encode-raw-h264 hve)


cmake_minimum_required(VERSION 3.0)

project(
    hve
)

add_library(hve hve.c)
target_link_libraries(hve avcodec avutil avfilter)
install(TARGETS hve DESTINATION lib)
install(FILES hve.h DESTINATION include)

add_executable(hve-encode-raw-h264 examples/hve_encode_raw_h264.c)
target_link_libraries(hve-encode-raw-h264 hve)

add_executable(hve-encode-raw-hevc10 examples/hve_encode_raw_hevc10.c)
target_link_libraries(hve-encode-raw-hevc10 hve)

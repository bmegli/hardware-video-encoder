cmake_minimum_required(VERSION 3.0)

project(
    hve
)

add_library(hve hve.c)
target_link_libraries(hve avcodec avutil)
install(TARGETS hve DESTINATION lib)
install(FILES hve.h DESTINATION include)

find_package( OpenCV REQUIRED )
INCLUDE_DIRECTORIES(${OpenCV_INCLUDE_DIRS})

add_executable(hve-encode-raw-h264 examples/hve_encode_raw_h264.cpp)
target_link_libraries(hve-encode-raw-h264 hve ${OpenCV_LIBS})

add_executable(hve-encode-raw-hevc10 examples/hve_encode_raw_hevc10.c)
target_link_libraries(hve-encode-raw-hevc10 hve)





add_library(sedsprintf STATIC
    telemetry_router.cpp
)

target_include_directories(sedsprintf PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
    $<INSTALL_INTERFACE:include>
)

file(GLOB_RECURSE SEDS_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_library(sedsprintf STATIC ${SEDS_SOURCES})
add_library(sedsprintf::sedsprintf ALIAS sedsprintf)

set_target_properties(sedsprintf PROPERTIES
        OUTPUT_NAME sedsprintf
        POSITION_INDEPENDENT_CODE ON
)

target_include_directories(sedsprintf
        PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/C-Headers>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(sedsprintf PUBLIC cxx_std_17)

# (optional install rules)
include(GNUInstallDirs)
install(TARGETS sedsprintf EXPORT sedsprintfTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY ${PROJECT_SOURCE_DIR}/C-Headers/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT sedsprintfTargets
        NAMESPACE sedsprintf::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sedsprintf
)

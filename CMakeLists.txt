cmake_minimum_required(VERSION 2.8)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum OS X deployment version")
project(VideoInspect)

set(CMAKE_CXX_STANDARD 14)

set(VideoInspect_VERSION 1.0.0)

add_subdirectory(src)
add_subdirectory(lib/ddui)
add_subdirectory(lib/portaudio ${CMAKE_CURRENT_BINARY_DIR}/portaudio EXCLUDE_FROM_ALL)
add_subdirectory(lib/FFmpeg)

add_definitions(-DGL_SILENCE_DEPRECATION)

list(APPEND ASSETS
    ${ddui_ASSETS}
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/PTMono.ttf
)

if(APPLE)

    set(ICON ${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.icns)
    set(MACOSX_BUNDLE_ICON_FILE icon.icns)
    set_source_files_properties(${ICON} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )

    set_source_files_properties(
        ${ASSETS}
        PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources/assets
    )
    set(MACOSX_BUNDLE_BUNDLE_NAME "Video Inspector")
    
    add_executable(VideoInspect MACOSX_BUNDLE ${SOURCES} ${ASSETS} ${ICON})

    set_target_properties(VideoInspect PROPERTIES
        MACOSX_BUNDLE_ICON_FILE icon.icns
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/assets/MacOSXBundleInfo.plist.in
        RESOURCE ${ICON}
    )

else()

    add_executable(VideoInspect WIN32 ${SOURCES} ${ASSETS})
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/bmjap.desktop.in
        ${CMAKE_CURRENT_BINARY_DIR}/bmjap.desktop
    )
    list(APPEND ASSETS
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.png
        ${CMAKE_CURRENT_BINARY_DIR}/bmjap.desktop
    )
    file(COPY ${ASSETS} DESTINATION "assets/")

endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/portaudio/include/
)

target_link_libraries(VideoInspect
    ddui
    portaudio_static
    FFmpeg
)

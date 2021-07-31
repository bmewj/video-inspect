list(APPEND SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/audio_client.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/audio_client.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/video_reader.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/video_reader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/peak_image.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/peak_image.cpp
)
add_subdirectory(data_types)
set(SOURCES ${SOURCES} PARENT_SCOPE)

cmake_minimum_required(VERSION 3.0.0)
project(vdisk_mock VERSION 0.1.0)

include(CTest)
enable_testing()


add_compile_definitions(_GNU_SOURCE  )


set(Source
    "comms_manager.c"
    "main.c"
    "MQTT-C/src/mqtt.c" 
    "MQTT-C/src/mqtt_pal.c"
    "rpa_queue.c"
    "utils.c"
)
source_group("Source" FILES ${Source})

set(ALL_FILES
    ${Source}
)

add_executable(${PROJECT_NAME} ${ALL_FILES})

target_include_directories(${PROJECT_NAME} PUBLIC . )
target_link_libraries(${PROJECT_NAME} pthread gcc_s c )

include_directories(${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/MQTT-C/include)
# add_compile_definitions(MQTT_USE_WOLFSSL)

set_source_files_properties(MQTT-C/src/mqtt.c PROPERTIES COMPILE_FLAGS -Wno-conversion)
set_source_files_properties(MQTT-C/src/mqtt_pal.c PROPERTIES COMPILE_FLAGS -Wno-conversion)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)

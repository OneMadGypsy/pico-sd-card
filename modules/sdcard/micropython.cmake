add_library(usermod_sdcard INTERFACE)

target_sources(usermod_sdcard INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/sdcard.c
)

target_include_directories(usermod_sdcard INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_compile_definitions(usermod_sdcard INTERFACE
    -DMODULE_SDCARD_ENABLED=1
)

target_link_libraries(usermod INTERFACE usermod_sdcard)

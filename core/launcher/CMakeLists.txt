if(NOT WIN32)
    string(REPLACE "-static " "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ")
    string(STRIP "${CMAKE_EXE_LINKER_FLAGS}" CMAKE_EXE_LINKER_FLAGS)
endif()

add_executable(launcher
        ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Ui.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Dialogs.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/App.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Library.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Json.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/stb_impl.cpp
        ${CMAKE_SOURCE_DIR}/3rdparty/imgui/imgui.cpp
        ${CMAKE_SOURCE_DIR}/3rdparty/imgui/imgui_draw.cpp
        ${CMAKE_SOURCE_DIR}/3rdparty/imgui/imgui_tables.cpp
        ${CMAKE_SOURCE_DIR}/3rdparty/imgui/imgui_widgets.cpp
        ${CMAKE_SOURCE_DIR}/3rdparty/imgui/backends/imgui_impl_sdl2.cpp
        ${CMAKE_SOURCE_DIR}/3rdparty/imgui/backends/imgui_impl_sdlrenderer2.cpp
)

target_include_directories(launcher PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_BINARY_DIR}/include
        ${CMAKE_SOURCE_DIR}/3rdparty/imgui
        ${CMAKE_SOURCE_DIR}/3rdparty/imgui/backends
        ${CMAKE_SOURCE_DIR}/3rdparty/stb
)

target_link_libraries(launcher PRIVATE SDL2-static)

if(BUILD_TESTING)
    add_test_executable(launcher_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/LauncherTests.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Library.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Json.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/App.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Ui.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Dialogs.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/stb_impl.cpp
        ${CMAKE_SOURCE_DIR}/3rdparty/imgui/imgui.cpp
        ${CMAKE_SOURCE_DIR}/3rdparty/imgui/imgui_draw.cpp
        ${CMAKE_SOURCE_DIR}/3rdparty/imgui/imgui_tables.cpp
        ${CMAKE_SOURCE_DIR}/3rdparty/imgui/imgui_widgets.cpp
    )
    target_include_directories(launcher_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_BINARY_DIR}/include
        ${CMAKE_SOURCE_DIR}/3rdparty/imgui
        ${CMAKE_SOURCE_DIR}/3rdparty/imgui/backends
        ${CMAKE_SOURCE_DIR}/3rdparty/stb
    )
    target_link_libraries(launcher_tests PRIVATE SDL2-static)
    add_test(NAME launcher COMMAND launcher_tests)
    add_test(NAME launcher_check COMMAND launcher --check)
endif()

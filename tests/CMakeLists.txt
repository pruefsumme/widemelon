cmake_minimum_required(VERSION 3.16)
project(WideMelonTests VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(WIDEMELON_ENABLE_STREAM_BENCHMARK
    "Build the optional Chromium streaming benchmark (requires a working DevTools browser)" OFF)

include(CTest)
if(BUILD_TESTING)
    add_executable(profile_test profile_test.cpp)
    target_include_directories(profile_test PRIVATE ../src)
    foreach(width 256 308 342 448 682 768)
        add_test(NAME profile_${width} COMMAND profile_test ${width})
        set_tests_properties(profile_${width} PROPERTIES ENVIRONMENT "WIDEMELON_VIEW_WIDTH=${width}")
    endforeach()
    foreach(bad 0 -1 257 10000 garbage 342x)
        add_test(NAME invalid_${bad} COMMAND profile_test 256)
        set_tests_properties(invalid_${bad} PROPERTIES ENVIRONMENT "WIDEMELON_VIEW_WIDTH=${bad}")
    endforeach()

    if(DEFINED USE_QT6)
        if(USE_QT6)
            set(QT_VERSION_MAJOR 6)
        else()
            set(QT_VERSION_MAJOR 5)
        endif()
    else()
        find_package(QT NAMES Qt6 Qt5 COMPONENTS Core Gui Network WebSockets REQUIRED)
    endif()
    find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Core Concurrent Gui Widgets Network WebSockets REQUIRED)
    add_executable(phone_protocol_test phone_protocol_test.cpp ../src/frontend/qt_sdl/PhoneSecurity.cpp)
    target_include_directories(phone_protocol_test PRIVATE ../src)
    target_link_libraries(phone_protocol_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Network)
    add_test(NAME phone_protocol COMMAND phone_protocol_test)

    add_executable(phone_transport_test phone_transport_test.cpp ../src/frontend/qt_sdl/PhoneSecurity.cpp)
    target_include_directories(phone_transport_test PRIVATE ../src)
    target_link_libraries(phone_transport_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::WebSockets)
    add_test(NAME phone_transport COMMAND phone_transport_test)

    add_executable(phone_bridge_test phone_bridge_test.cpp phone_test_services.cpp
        ../src/frontend/qt_sdl/PhoneBridge.cpp ../src/frontend/qt_sdl/PhoneSecurity.cpp
        ../src/frontend/qt_sdl/PhoneLayout.cpp ../src/frontend/qt_sdl/PhoneFirewall.cpp
        ../src/frontend/qt_sdl/PhoneScreenDialog.cpp ../src/frontend/qt_sdl/PhoneLayoutDialog.cpp
        ../src/frontend/qt_sdl/qrcodegen/qrcodegen.cpp
        ../src/frontend/qt_sdl/phone/phone.qrc)
    set_target_properties(phone_bridge_test PROPERTIES AUTOMOC ON AUTORCC ON)
    target_include_directories(phone_bridge_test PRIVATE ../src ../src/net ../src/frontend ../src/frontend/qt_sdl)
    # PhoneLayout uses the frontend's hotkey definitions through EmuInstance.h,
    # which includes SDL headers. They need an explicit path outside /usr/include.
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
    include(../cmake/FixInterfaceIncludes.cmake)
    fix_interface_includes(PkgConfig::SDL2)
    target_include_directories(phone_bridge_test PRIVATE
        $<TARGET_PROPERTY:PkgConfig::SDL2,INTERFACE_INCLUDE_DIRECTORIES>)
    target_link_libraries(phone_bridge_test PRIVATE Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Concurrent Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::WebSockets)
    get_target_property(QT_GUI_LIBRARY_TYPE Qt${QT_VERSION_MAJOR}::Gui TYPE)
    if(QT_GUI_LIBRARY_TYPE STREQUAL "STATIC_LIBRARY")
        qt_import_plugins(phone_protocol_test INCLUDE Qt::QJpegPlugin)
        qt_import_plugins(phone_bridge_test INCLUDE Qt::QJpegPlugin)
    endif()
    add_test(NAME phone_bridge COMMAND phone_bridge_test)
    if(APPLE)
        set(WIDEMELON_TEST_QPA cocoa)
    elseif(WIN32)
        set(WIDEMELON_TEST_QPA windows)
    else()
        set(WIDEMELON_TEST_QPA offscreen)
    endif()
    set_tests_properties(phone_bridge PROPERTIES TIMEOUT 20
        ENVIRONMENT "QT_QPA_PLATFORM=${WIDEMELON_TEST_QPA}")

    add_executable(phone_firewall_test phone_firewall_test.cpp ../src/frontend/qt_sdl/PhoneFirewall.cpp)
    target_include_directories(phone_firewall_test PRIVATE ../src)
    target_link_libraries(phone_firewall_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network)
    add_test(NAME phone_firewall COMMAND phone_firewall_test)

    find_program(NODE_EXECUTABLE node)
    if(NODE_EXECUTABLE)
        execute_process(COMMAND ${NODE_EXECUTABLE} --version OUTPUT_VARIABLE NODE_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
        string(REGEX REPLACE "^v" "" NODE_VERSION "${NODE_VERSION}")
    endif()
    if(NODE_EXECUTABLE AND NODE_VERSION VERSION_GREATER_EQUAL 18)
        add_test(NAME phone_browser COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/phone_browser_test.js)
    else()
        message(STATUS "Node.js 18+ not found; phone browser regression test is unavailable")
    endif()
    find_program(CHROMIUM_EXECUTABLE NAMES chromium chromium-browser)
    if(WIDEMELON_ENABLE_STREAM_BENCHMARK AND CHROMIUM_EXECUTABLE
        AND NODE_EXECUTABLE AND NODE_VERSION VERSION_GREATER_EQUAL 22)
        add_test(NAME phone_stream_pacing COMMAND ${NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/phone_browser_smoke.js $<TARGET_FILE:phone_bridge_test>
            ${CHROMIUM_EXECUTABLE} --benchmark --dialog)
        set_tests_properties(phone_stream_pacing PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE
            ENVIRONMENT "WIDEMELON_BENCH_QUALITY=100")
    endif()
endif()

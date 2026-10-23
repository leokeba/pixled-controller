# The controller's code: an ESP-IDF component when built by idf.py (the
# firmware in firmware/ adds this directory), host library + tests otherwise.
set(PIXLED_SRCS
    src/pixled/board.cpp
    src/pixled/chips.cpp
    src/pixled/mapping.cpp
    src/pixled/frames.cpp
    src/pixled/ddp.cpp
    src/pixled/dmx_input.cpp
    src/pixled/controller.cpp
    src/pixled/tree.cpp)

if(ESP_PLATFORM)
    # TesserAPI and TesserKIT, under whatever component name they were added
    # with (a directory in EXTRA_COMPONENT_DIRS, or the component manager's
    # "<namespace>__<name>"), as TesserKIT finds its own. led_strip comes from
    # the component manager (idf_component.yml).
    idf_build_get_property(PIXLED_BUILD_COMPONENTS BUILD_COMPONENTS)
    set(PIXLED_TESSERAPI "")
    set(PIXLED_TESSERKIT "")
    foreach(c ${PIXLED_BUILD_COMPONENTS})
        string(TOLOWER "${c}" c_lower)
        if(c_lower MATCHES "(^|__)tesserapi$")
            set(PIXLED_TESSERAPI ${c})
        elseif(c_lower MATCHES "(^|__)tesserkit$")
            set(PIXLED_TESSERKIT ${c})
        endif()
    endforeach()
    if((NOT PIXLED_TESSERAPI OR NOT PIXLED_TESSERKIT) AND NOT CMAKE_BUILD_EARLY_EXPANSION)
        message(FATAL_ERROR "pixled-controller needs TesserAPI and TesserKIT as components")
    endif()
    idf_component_register(SRCS ${PIXLED_SRCS}
                                src/pixled/platform/esp/led_strip_bus.cpp
                                src/pixled/platform/esp/ddp_esp.cpp
                                src/pixled/platform/esp/output_task_esp.cpp
                                src/pixled/platform/esp/relay_esp.cpp
                           INCLUDE_DIRS "src"
                           PRIV_REQUIRES esp_timer lwip esp_driver_gpio)
    if(PIXLED_TESSERAPI AND PIXLED_TESSERKIT)
        foreach(dep ${PIXLED_TESSERAPI} ${PIXLED_TESSERKIT})
            idf_component_get_property(lib ${dep} COMPONENT_LIB)
            target_link_libraries(${COMPONENT_LIB} PUBLIC ${lib})
        endforeach()
    endif()
    return()
endif()

cmake_minimum_required(VERSION 3.16)
project(pixled_controller CXX)

option(PIXLED_SANITIZE "Build with ASan/UBSan" ON)
set(TESSERKIT_DIR "" CACHE PATH "TesserKIT source tree (default: ../TesserKIT)")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# TesserKIT, which brings TesserAPI (../TesserAPI next to it, or
# TESSERAPI_DIR), and its fake platform with the tests' harness (check.h).
if(NOT TESSERKIT_DIR)
    get_filename_component(TESSERKIT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../TesserKIT" ABSOLUTE)
endif()
if(NOT EXISTS "${TESSERKIT_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "TesserKIT not found at ${TESSERKIT_DIR}: clone it there, or set TESSERKIT_DIR")
endif()
set(TESSERKIT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${TESSERKIT_DIR}" "${CMAKE_BINARY_DIR}/tesserkit")

# Each checkout against deps.lock: a warning when it isn't the pinned commit.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/deps.cmake")
pixled_check_sibling(TesserKIT "${TESSERKIT_DIR}")
pixled_check_sibling(TesserAPI "${TESSERAPI_DIR}")
get_filename_component(TESSERUI_CHECKOUT "${CMAKE_CURRENT_SOURCE_DIR}/../TesserUI" ABSOLUTE)
pixled_check_sibling(TesserUI "${TESSERUI_CHECKOUT}")

set(PIXLED_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror -fno-exceptions -fno-rtti)

add_library(pixled_core ${PIXLED_SRCS})
target_include_directories(pixled_core PUBLIC src)
target_link_libraries(pixled_core PUBLIC tesserkit_core)
set_target_properties(pixled_core PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_compile_options(pixled_core PRIVATE ${PIXLED_WARNINGS})

enable_testing()
add_executable(pixled_tests test/test_main.cpp)
target_link_libraries(pixled_tests PRIVATE pixled_core tesserkit_fake)
set_target_properties(pixled_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_options(pixled_tests PRIVATE -Wall -Wextra -Werror -fno-exceptions -fno-rtti)
add_test(NAME pixled_tests COMMAND pixled_tests)

if(PIXLED_SANITIZE)
    # Every library in the binary, so it is instrumented alike.
    foreach(t tesser_core tesserkit_core tesserkit_fake pixled_core pixled_tests)
        target_compile_options(${t} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -g)
        target_link_options(${t} PRIVATE -fsanitize=address,undefined)
    endforeach()
endif()

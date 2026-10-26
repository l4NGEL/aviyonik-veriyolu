cmake_minimum_required(VERSION 3.20)
project(aviyonik_veriyolu VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(AVB_WERROR "Uyarilari hata say" OFF)
option(AVB_COVERAGE "Kutuphaneyi satir/dal/MC-DC kapsamasiyla derle (GCC 14+)" OFF)
option(AVB_BUILD_RTOS "FreeRTOS POSIX ornegini derle (FreeRTOS-Kernel indirilir)" ON)
set(AVB_SANITIZE "" CACHE STRING "Sanitizer listesi, ornek: address,undefined")

# ---------------------------------------------------------------- uyarilar
add_library(avb_warnings INTERFACE)
target_compile_options(avb_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
    -Wold-style-cast -Wcast-align -Wnull-dereference -Wdouble-promotion
    -Wformat=2 -Wimplicit-fallthrough)
if(AVB_WERROR)
    target_compile_options(avb_warnings INTERFACE -Werror)
endif()

if(AVB_SANITIZE)
    add_compile_options(-fsanitize=${AVB_SANITIZE} -fno-omit-frame-pointer)
    add_link_options(-fsanitize=${AVB_SANITIZE})
endif()

# ---------------------------------------------------------------- kutuphane
add_library(avb STATIC
    src/m1553/word.cpp
    src/m1553/manchester.cpp
    src/m1553/bus.cpp
    src/m1553/remote_terminal.cpp
    src/m1553/bus_controller.cpp
    src/can/signal.cpp
    src/can/isotp.cpp
    src/can/socketcan.cpp)
target_include_directories(avb PUBLIC include)
target_link_libraries(avb PRIVATE avb_warnings)

if(AVB_COVERAGE)
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
        message(FATAL_ERROR "MC/DC kapsamasi icin GCC 14+ gerekir (-fcondition-coverage)")
    endif()
    target_compile_options(avb PRIVATE -O0 --coverage -fcondition-coverage)
    target_link_options(avb PUBLIC --coverage)
endif()

# ---------------------------------------------------------------- testler
enable_testing()
find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(avb_tests
    tests/m1553_word_test.cpp
    tests/m1553_manchester_test.cpp
    tests/m1553_terminal_test.cpp
    tests/m1553_schedule_test.cpp
    tests/m1553_fault_test.cpp
    tests/can_signal_test.cpp
    tests/can_isotp_test.cpp
    tests/can_socketcan_test.cpp)
target_link_libraries(avb_tests PRIVATE avb avb_warnings GTest::gtest_main)
gtest_discover_tests(avb_tests DISCOVERY_TIMEOUT 30)

# Kararli durumda dinamik bellek ayrilmadigini global new sayaciyla dogrular;
# operator new'i ezdigi icin ayri calistirilabilir.
add_executable(avb_alloc_test tests/alloc_test.cpp)
target_link_libraries(avb_alloc_test PRIVATE avb avb_warnings)
add_test(NAME sifir_dinamik_bellek COMMAND avb_alloc_test)

# ---------------------------------------------------------------- ornekler
add_executable(m1553_demo apps/m1553_demo.cpp)
target_link_libraries(m1553_demo PRIVATE avb avb_warnings)

if(AVB_BUILD_RTOS)
    add_subdirectory(rtos)
endif()

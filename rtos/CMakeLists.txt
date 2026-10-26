# FreeRTOS-Kernel POSIX portu üzerinde RT uygulaması ve öncelik tersinmesi deneyi.
include(FetchContent)
find_package(Threads REQUIRED)

add_library(freertos_config INTERFACE)
target_include_directories(freertos_config SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/config)

set(FREERTOS_PORT GCC_POSIX CACHE STRING "FreeRTOS portu" FORCE)
set(FREERTOS_HEAP 4 CACHE STRING "FreeRTOS yigin yonetimi" FORCE)
FetchContent_Declare(freertos_kernel
    GIT_REPOSITORY https://github.com/FreeRTOS/FreeRTOS-Kernel.git
    GIT_TAG V11.1.0
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(freertos_kernel)

add_executable(rtos_terminal main.cpp)
target_link_libraries(rtos_terminal PRIVATE freertos_kernel freertos_config avb Threads::Threads)
# FreeRTOS makroları (pdMS_TO_TICKS, pdTRUE) C tarzı dönüşüm içerir: bu dosyada
# -Wold-style-cast ve -Wconversion ailesi kapalı, diğer uyarılar açık
target_compile_options(rtos_terminal PRIVATE -Wall -Wextra -Wshadow $<$<BOOL:${AVB_WERROR}>:-Werror>)

add_test(NAME rtos_gorevler_5sn COMMAND rtos_terminal --sure 5)
add_test(NAME rtos_oncelik_tersinmesi COMMAND rtos_terminal --oncelik)
set_tests_properties(rtos_gorevler_5sn rtos_oncelik_tersinmesi PROPERTIES TIMEOUT 60 ENVIRONMENT "AVB_VCAN=vcan0")

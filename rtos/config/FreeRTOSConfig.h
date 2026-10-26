#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* FreeRTOS POSIX (GCC_POSIX) portu için yapılandırma.
 * Görevler Linux pthread'leri üzerinde koşar; zamanlayıcı yine FreeRTOS'un
 * öncelik tabanlı, kesintili (preemptive) zamanlayıcısıdır. Gerçek zamanlı
 * garanti YOKTUR: tik sinyalle üretilir ve Linux zamanlamasına bağlıdır. */

#define configUSE_PREEMPTION 1
#define configUSE_TIME_SLICING 1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define configUSE_IDLE_HOOK 0
#define configUSE_TICK_HOOK 0
#define configTICK_RATE_HZ ((TickType_t)1000)
#define configMAX_PRIORITIES 7
#define configMINIMAL_STACK_SIZE ((unsigned short)1024)
#define configTOTAL_HEAP_SIZE ((size_t)(256 * 1024))
#define configMAX_TASK_NAME_LEN 16
#define configTICK_TYPE_WIDTH_IN_BITS TICK_TYPE_WIDTH_32_BITS
#define configIDLE_SHOULD_YIELD 1

#define configUSE_MUTEXES 1
#define configUSE_RECURSIVE_MUTEXES 0
#define configUSE_COUNTING_SEMAPHORES 1
#define configQUEUE_REGISTRY_SIZE 0
#define configUSE_TRACE_FACILITY 1
#define configUSE_STATS_FORMATTING_FUNCTIONS 0
#define configCHECK_FOR_STACK_OVERFLOW 0
#define configUSE_MALLOC_FAILED_HOOK 1
#define configSUPPORT_DYNAMIC_ALLOCATION 1
#define configSUPPORT_STATIC_ALLOCATION 0
#define configUSE_TIMERS 0
#define configUSE_CO_ROUTINES 0

#define INCLUDE_xTaskDelayUntil 1
#define INCLUDE_vTaskDelay 1
#define INCLUDE_vTaskDelete 1
#define INCLUDE_vTaskSuspend 1
#define INCLUDE_uxTaskPriorityGet 1
#define INCLUDE_xTaskGetCurrentTaskHandle 1
#define INCLUDE_xTaskGetSchedulerState 1

void vAssertCalled(const char* file, unsigned long line);
#define configASSERT(x)                         \
    if ((x) == 0) {                             \
        vAssertCalled(__FILE__, __LINE__);      \
    }

#endif

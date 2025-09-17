/**
 * @file main.c
 * @brief Demonstration of monitoring FreeRTOS heap usage with static task creation.
 *
 * This example creates a FreeRTOS task using xTaskCreateStatic() to avoid
 * dynamic memory allocation. The task periodically logs the current free
 * heap size and the minimum ever free heap size, which can be useful for
 * debugging and tuning memory usage in embedded applications.
 *
 * Key APIs demonstrated:
 *   - xPortGetFreeHeapSize(): Get the current free heap size.
 *   - xPortGetMinimumEverFreeHeapSize(): Get the lowest free heap size recorded since startup.
 *   - xTaskCreateStatic(): Create a FreeRTOS task using statically allocated memory.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define TAG "monitor-heap-demo"

// In ESP-IDF, stack size is in **bytes** (not words)
#define STACK_SIZE_BYTES (3 * 1024)

// Task control block (TCB) for the task
static StaticTask_t heap_monitor_tcb;

// Stack memory for the task
static StackType_t heap_monitor_stack[STACK_SIZE_BYTES];

/**
 * @brief Task function that monitors and logs heap usage.
 *
 * This task runs in an infinite loop. On each iteration, it queries the
 * current free heap size and the minimum ever recorded free heap size,
 * then logs these values. It delays for one second between iterations.
 *
 * @param[in] pvParameters Pointer to optional parameters (unused).
 */
static void heap_monitor_task(void *pvParameters) {
    (void) pvParameters;
    for (;;) {
        // Get current free heap size
        size_t free_heap = xPortGetFreeHeapSize();
        // Get minimum ever free heap size
        size_t min_free_heap = xPortGetMinimumEverFreeHeapSize();
        // Log the heap sizes
        ESP_LOGI(TAG, "Current free heap: %u bytes, Minimum ever free heap: %u bytes",
                 (unsigned)free_heap, (unsigned)min_free_heap);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/**
 * @brief Main application entry point.
 *
 * This function creates the heap monitor task using xTaskCreateStatic(),
 * providing statically allocated memory for both the task control block (TCB)
 * and stack. If task creation fails, it logs an error message.
 */
void app_main(void) {
    // Create task without using the heap
    TaskHandle_t task_handle = xTaskCreateStatic(
        heap_monitor_task,   // Task function
        "HeapMonitor",       // Name
        STACK_SIZE_BYTES,    // Stack size in bytes
        NULL,                // Parameters
        5,                   // Priority
        heap_monitor_stack,  // Stack buffer
        &heap_monitor_tcb    // TCB buffer
    );

    if (task_handle == NULL) {
        ESP_LOGE(TAG, "Failed to create heap monitor task!");
    }
}
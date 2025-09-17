/**
 * @file main.c
 * @brief Demonstration of using ESP-IDF heap capabilities API.
 *
 * This example creates a static FreeRTOS task that periodically reports heap
 * memory statistics. It shows how to use:
 *   - heap_caps_get_free_size(MALLOC_CAP_8BIT): to query free memory of a given capability.
 *   - heap_caps_print_heap_info(MALLOC_CAP_8BIT): to print detailed heap usage.
 *
 * The task runs every 2 seconds, logging the available 8-bit capable memory
 * and printing a detailed summary of heap blocks. This example is intended
 * for educational purposes to help understand memory management in ESP-IDF.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#define TAG "heap-caps-demo"

// In ESP-IDF, stack size is in bytes
#define MONITOR_STACK_SIZE (3 * 1024)

// Task control block (TCB) for the task
static StaticTask_t monitor_tcb;

// Stack memory for the task
static StackType_t monitor_stack[MONITOR_STACK_SIZE];

/**
 * @brief Task function that monitors and prints heap memory statistics.
 *
 * This task periodically queries the amount of free heap memory available
 * with 8-bit access capability using heap_caps_get_free_size(), and prints
 * a detailed summary of heap memory blocks with heap_caps_print_heap_info().
 *
 * @param[in] pvParameters Unused parameter, kept for FreeRTOS API compliance.
 */
static void heap_caps_monitor_task(void *pvParameters) {
    (void) pvParameters;

    for (;;) {
        // Get the size of free heap with 8-bit capability
        size_t free_8bit = heap_caps_get_free_size(MALLOC_CAP_8BIT);
        ESP_LOGI(TAG, "Free 8-bit capable heap: %u bytes", (unsigned)free_8bit);
        ESP_LOGI(TAG, "Detailed heap info (8-bit capable):");
        // Get and print detailed heap info
        heap_caps_print_heap_info(MALLOC_CAP_8BIT);

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

/**
 * @brief Main application entry point.
 *
 * This function creates the heap monitor task statically using xTaskCreateStatic().
 * It allocates a task control block (TCB) and stack memory from global static
 * storage. If task creation fails, an error message is logged.
 */
void app_main(void) {
    // Create task without using the heap
    TaskHandle_t handle = xTaskCreateStatic(
        heap_caps_monitor_task,   // Task function
        "HeapCapsMonitor",        // Name
        MONITOR_STACK_SIZE,       // Stack size in bytes
        NULL,                     // Parameters
        5,                        // Priority
        monitor_stack,            // Stack buffer
        &monitor_tcb              // TCB buffer
    );

    if (handle == NULL) {
        ESP_LOGE(TAG, "Failed to create heap caps monitor task!");
    }
}
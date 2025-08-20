/**
 * @file task_priority_example.c
 * @brief Demonstrates FreeRTOS task priorities on ESP32.
 *
 * This example creates two tasks with different priorities:
 *  1. task_low  – Low-priority task that runs every 1 second.
 *  2. task_high – High-priority task that runs every 0.5 seconds.
 *
 * The output shows how FreeRTOS schedules tasks based on priority.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/**
 * @brief Low-priority task that runs every second.
 *
 * Prints the CPU core it is running on and then delays for 1 second.
 *
 * @param pvParameter Pointer to optional task parameters (unused here).
 */
void task_low(void *pvParameter) {
    while (1) {
        printf("Low priority task running on Core %d\n", xPortGetCoreID());
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/**
 * @brief High-priority task that runs every 0.5 seconds.
 *
 * Prints the CPU core it is running on and then delays for 500 ms.
 *
 * @param pvParameter Pointer to optional task parameters (unused here).
 */
void task_high(void *pvParameter) {
    while (1) {
        printf("High priority task running on Core %d\n", xPortGetCoreID());
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/**
 * @brief Main application entry point.
 *
 * Creates two tasks with different priorities to demonstrate
 * FreeRTOS scheduling behavior.
 */
void app_main() {
    // Low priority task (priority 3)
    xTaskCreate(task_low, "LowPriority", 2048, NULL, 3, NULL);

    // High priority task (priority 8)
    xTaskCreate(task_high, "HighPriority", 2048, NULL, 8, NULL);
}
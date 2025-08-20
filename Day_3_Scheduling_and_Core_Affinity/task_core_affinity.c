/**
 * @file task_core_affinity.c
 * @brief Demonstrates creating FreeRTOS tasks with and without CPU core affinity on ESP32.
 *
 * This example creates two FreeRTOS tasks:
 *  1. An unpinned task that can run on any available core.
 *  2. A pinned task that always runs on Core 1.
 * Each task prints the core it is running on every second.
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/**
 * @brief Task that runs on any available core.
 *
 * Prints the ID of the core it is currently executing on
 * every second using vTaskDelay for timing.
 *
 * @param pvParameters Pointer to optional task parameters (unused here).
 */
void task_unpinned(void *pvParameters) {
    while (1) {
        printf("Unpinned Task running on Core %d\n", xPortGetCoreID());
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/**
 * @brief Task that is pinned to Core 1.
 *
 * Always runs on Core 1 and prints the core ID every second
 * using vTaskDelay for timing.
 *
 * @param pvParameters Pointer to optional task parameters (unused here).
 */
void task_pinned_core1(void *pvParameters) {
    while (1) {
        printf("Pinned Task running on Core %d\n", xPortGetCoreID());
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/**
 * @brief Main application entry point.
 *
 * Creates an unpinned task and a Core 0 pinned task, each with
 * a stack size of 2048 bytes and priority 5.
 */
void app_main() {
    // Task without core affinity (runs on any available core)
    xTaskCreate(task_unpinned, "Task Unpinned", 2048, NULL, 5, NULL);

    // Task pinned to Core 1
    xTaskCreatePinnedToCore(task_pinned_core1, "Task Core0", 2048, NULL, 5, NULL, 1);
}
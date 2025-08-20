/**
 * @file task_priority_suspend_example.c
 * @brief Demonstrates FreeRTOS task priorities and suspension on ESP32.
 *
 * This example creates two tasks with different priorities:
 *  - task_low  (priority 3): prints once per second.
 *  - task_high (priority 8): prints twice per second and, periodically,
 *    suspends task_low for 3 seconds to highlight preemption and control.
 *
 * Observe the console: when task_low is suspended, only task_high prints.
 * 
 * Notes:
 *   vTaskSuspend()/vTaskResume() act immediately; the suspended task won’t be scheduled until resumed.
 *   Keep configUSE_PREEMPTION = 1 (default in ESP-IDF) to clearly observe priority preemption.
 *   Avoid suspending critical system tasks (Idle/Timer). Here we only suspend our own task_low
 * 
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Handle for the low-priority task so the high-priority task can control it.
static TaskHandle_t g_low_task_handle = NULL;

/**
 * @brief Low-priority task that prints every second.
 *
 * Prints which CPU core it is running on, then delays for 1000 ms.
 * This task may be suspended/resumed by the high-priority task.
 *
 * @param pvParameter Optional parameter (unused).
 */
void task_low(void *pvParameter) {
    (void)pvParameter;
    while (1) {
        printf("[LOW ] Core %d: running\n", xPortGetCoreID());
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/**
 * @brief High-priority task that periodically suspends the low-priority task.
 *
 * Prints every 500 ms. Every ~3 seconds (after several iterations),
 * it suspends task_low for 3 seconds to demonstrate task control,
 * then resumes it.
 *
 * @param pvParameter Optional parameter (unused).
 */
void task_high(void *pvParameter) {
    (void)pvParameter;

    int iter = 0;
    const TickType_t suspend_time = pdMS_TO_TICKS(3000);

    while (1) {
        printf("[HIGH] Core %d: running (iter=%d)\n", xPortGetCoreID(), iter);

        // Every 6 iterations (~3 seconds at 500 ms period), suspend low task for 3 seconds.
        if ((iter % 6) == 0 && g_low_task_handle != NULL) {
            printf("[HIGH] Suspending LOW task for 3 seconds...\n");
            vTaskSuspend(g_low_task_handle);

            // Keep printing while LOW is suspended to show it is paused.
            TickType_t start = xTaskGetTickCount();
            while ((xTaskGetTickCount() - start) < suspend_time) {
                printf("[HIGH] LOW task is suspended...\n");
                vTaskDelay(pdMS_TO_TICKS(500));
            }

            printf("[HIGH] Resuming LOW task now.\n");
            vTaskResume(g_low_task_handle);
        }

        vTaskDelay(pdMS_TO_TICKS(500));
        iter++;
    }
}

/**
 * @brief Main application entry point.
 *
 * Creates two tasks with different priorities. The high-priority task
 * periodically suspends and resumes the low-priority task to make
 * scheduling effects obvious in the console output.
 */
void app_main() {
    // Low priority task (priority 3)
    xTaskCreate(
        task_low,           // Task function
        "LowPriority",      // Name
        2048,               // Stack size (words)
        NULL,               // Parameter
        3,                  // Priority
        &g_low_task_handle  // Handle to control suspension/resumption
    );

    // High priority task (priority 8)
    xTaskCreate(
        task_high,          // Task function
        "HighPriority",     // Name
        2048,               // Stack size (words)
        NULL,               // Parameter
        8,                  // Priority
        NULL                // No handle needed
    );
}
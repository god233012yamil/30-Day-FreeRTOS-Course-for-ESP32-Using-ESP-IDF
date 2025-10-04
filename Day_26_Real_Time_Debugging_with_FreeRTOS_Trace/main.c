/**
 * @file main.c
 * @brief FreeRTOS Runtime Statistics Example on ESP32
 *
 * This example demonstrates how to collect and display runtime statistics
 * of FreeRTOS tasks using `vTaskGetRunTimeStats()`. It creates three tasks:
 * - BusyTask: Simulates continuous CPU work while yielding periodically.
 * - IdleLikeTask: Sleeps most of the time to represent an idle workload.
 * - StatsTask: Periodically collects and prints runtime statistics.
 *
 * Requirements (menuconfig):
 * - Component config → FreeRTOS → Enable run-time stats collection (ON).
 * - Use esp_timer for run-time stats (ON).
 *
 * This code avoids watchdog triggers by ensuring BusyTask periodically
 * yields with `vTaskDelay()`.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define TAG "DAY26"

/**
 * @brief Task that simulates continuous CPU work.
 *
 * This task executes a dummy workload in a loop and then calls `vTaskDelay(1)`
 * to yield the CPU to other tasks and prevent watchdog timeouts.
 *
 * @param pvParameters Pointer to task parameters (unused).
 */
static void busy_task(void *pvParameters)
{
    (void)pvParameters;
    while (1) {
        // Simulate CPU work by burning cycles
        for (volatile int i = 0; i < 100000; i++) {
            // no-op
        }
        // Yield to allow other tasks and idle task to run
        vTaskDelay(1);
    }
}

/**
 * @brief Task that mimics idle-like behavior.
 *
 * This task simply delays in a loop, consuming minimal CPU time.
 *
 * @param pvParameters Pointer to task parameters (unused).
 */
static void idle_like_task(void *pvParameters)
{
    (void)pvParameters;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/**
 * @brief Task that collects and displays FreeRTOS runtime statistics.
 *
 * This task uses `vTaskGetRunTimeStats()` to gather per-task CPU usage
 * and prints the results every 5 seconds.
 *
 * @param pvParameters Pointer to task parameters (unused).
 */
static void stats_task(void *pvParameters)
{
    (void)pvParameters;
    char buffer[1024];  // Buffer to hold formatted statistics

    while (1) {
        ESP_LOGI(TAG, "----- Runtime Stats (time, %% CPU) -----");
        vTaskGetRunTimeStats(buffer);
        ESP_LOGI(TAG, "\n%s", buffer);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

/**
 * @brief Application entry point.
 *
 * Creates three tasks with different behaviors to demonstrate FreeRTOS
 * runtime statistics. Also logs an initial message to confirm startup.
 */
void app_main(void)
{
    xTaskCreate(busy_task,      "BusyTask",   2048, NULL, 5, NULL);
    xTaskCreate(idle_like_task, "IdleTask",   2048, NULL, 4, NULL);
    xTaskCreate(stats_task,     "StatsTask",  4096, NULL, 3, NULL);

    ESP_LOGI(TAG, "Day 26: Runtime stats collection started");
}
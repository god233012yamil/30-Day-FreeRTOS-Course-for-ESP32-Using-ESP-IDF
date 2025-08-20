/**
 * @file delay_vs_delayuntil.c
 * @brief Compare FreeRTOS vTaskDelay (relative delay) vs vTaskDelayUntil (absolute periodic schedule) on ESP32.
 *
 * The program starts two tasks. One uses vTaskDelay() which may drift over time,
 * and the other uses vTaskDelayUntil() to keep a fixed 1 s cadence. Both print
 * timestamps in milliseconds derived from the RTOS tick count.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/**
 * @brief Task that delays relatively using vTaskDelay().
 *
 * Prints the current uptime in milliseconds and then sleeps for 1 second
 * using a relative delay. Over long runs, the print period may accumulate drift.
 *
 * @param pvParameter Optional task parameter (unused).
 */
void task_delay(void *pvParameter) {
    while (1) {
        printf("vTaskDelay: %lu ms\n", (unsigned long)(xTaskGetTickCount() * portTICK_PERIOD_MS));
        vTaskDelay(pdMS_TO_TICKS(1000)); // Delay 1s
    }
}

/**
 * @brief Task that delays on an absolute schedule using vTaskDelayUntil().
 *
 * Maintains a stable 1-second period referenced to the initial wake time,
 * minimizing drift between iterations. Prints the current uptime in ms.
 *
 * @param pvParameter Optional task parameter (unused).
 */
void task_delay_until(void *pvParameter) {
    TickType_t last_wake = xTaskGetTickCount();
    while (1) {
        printf("vTaskDelayUntil: %lu ms\n", (unsigned long)(xTaskGetTickCount() * portTICK_PERIOD_MS));
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000)); // Delay until next second
    }
}

/**
 * @brief Application entry point.
 *
 * Creates two tasks at priority 5: one using vTaskDelay() and one using
 * vTaskDelayUntil() to illustrate the difference in timing behavior.
 */
void app_main() {
    xTaskCreate(task_delay, "TaskDelay", 2048, NULL, 5, NULL);
    xTaskCreate(task_delay_until, "TaskDelayUntil", 2048, NULL, 5, NULL);
}
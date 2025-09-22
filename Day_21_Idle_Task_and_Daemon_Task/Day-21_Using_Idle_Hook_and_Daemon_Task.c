/**
 * @file main.c
 * @brief Demonstrates the use of the FreeRTOS Idle Task and Timer Daemon Task.
 *
 * This example shows:
 *   - How to use the Idle Hook function to execute code whenever the CPU is idle.
 *   - How to create a software timer that runs its callback in the Daemon (Timer Service) Task.
 *
 * The project sets up a periodic timer and uses the Idle Hook to periodically
 * perform lightweight background work. This pattern is useful for scheduling
 * both high-level periodic tasks (via timers) and opportunistic background
 * activity (via the idle task).
 * 
 * @note Ensure CONFIG_FREERTOS_USE_IDLE_HOOK is enabled in sdkconfig.
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"

#define TAG "DAY21"

// Timer handle
TimerHandle_t periodic_timer;

/**
 * @brief Idle hook function executed by the FreeRTOS Idle Task.
 *
 * This function is automatically called whenever the system is idle
 * (i.e., no higher-priority tasks are runnable). It is defined as a
 * weak symbol in FreeRTOS, so providing this implementation replaces
 * the default behavior.
 *
 * The function prints a dot periodically as an indication of idle
 * time. It must not block, allocate memory, or call functions that
 * might block.
 *
 * @note Ensure CONFIG_FREERTOS_USE_IDLE_HOOK is enabled in sdkconfig.
 */
void vApplicationIdleHook(void) {
    // Print a dot occasionally when system is idle
    static int counter = 0;
    counter++;
    if (counter > 100) {// Adjust this value as needed
        printf(".");
        fflush(stdout); // Ensure the dot is printed immediately
        counter = 0;
    }
}

/**
 * @brief Timer callback function executed in the Daemon Task context.
 *
 * This function is invoked by the FreeRTOS Timer Service Task whenever
 * the associated timer expires. In this example, it logs a message to
 * indicate the timer has fired.
 *
 * @param xTimer Handle to the timer that triggered the callback.
 */
void periodic_callback(TimerHandle_t xTimer) {
    ESP_LOGI(TAG, "Daemon task running periodic callback!");
}

/**
 * @brief Application entry point for the FreeRTOS example.
 *
 * This function creates and starts a periodic software timer. No
 * additional user tasks are created, so the Idle Task will execute
 * whenever the system is otherwise idle.
 *
 * Steps:
 *   1. Create a periodic timer with a 2-second interval.
 *   2. Start the timer so its callback executes in the Daemon Task.
 *
 * @note The Idle Hook will run in the background when no other tasks
 *       are scheduled.
 */
void app_main() {
    // Create and start a periodic timer
    periodic_timer = xTimerCreate("Periodic",
                                  pdMS_TO_TICKS(2000),
                                  pdTRUE,
                                  NULL,
                                  periodic_callback);

    if (periodic_timer != NULL) {
        xTimerStart(periodic_timer, 0);
    }

    // No user tasks created – Idle Task will run in background
}
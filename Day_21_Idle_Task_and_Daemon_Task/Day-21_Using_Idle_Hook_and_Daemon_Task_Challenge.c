/**
 * @file main.c
 * @brief Demonstrates the use of FreeRTOS Idle Task and Timer Daemon Task with multiple software timers.
 *
 * This example shows:
 *   - How to create multiple software timers that run their callbacks in the FreeRTOS Timer Daemon Task.
 *   - How to use the Idle Hook to toggle an LED whenever the CPU is idle.
 *
 * Features:
 *   - Two auto-reload timers with intervals of 1s and 3s, respectively.
 *   - Idle Hook toggles an LED every ~200 ms when the system is idle.
 *
 * @note Ensure `configUSE_IDLE_HOOK` is enabled in FreeRTOSConfig.h to use the Idle Hook.
 * @note Adjust `LED_GPIO` to match your board’s available LED pin (GPIO2 is common).
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_timer.h"   // for esp_timer_get_time()

#define TAG "DAY21"

// ====== LED pin (change if your board uses a different LED) ======
#ifndef LED_GPIO
#define LED_GPIO 2
#endif

// ====== Timer handles ======
static TimerHandle_t timer_1s = NULL;
static TimerHandle_t timer_3s = NULL;

/**
 * @brief Timer callback executed in the Daemon Task context.
 *
 * This function is triggered whenever a software timer expires. Since
 * the callback runs in the FreeRTOS Timer Daemon Task, it must not
 * block for long periods.
 *
 * The function logs which timer fired, based on its assigned period.
 *
 * @param xTimer Handle of the timer that triggered the callback.
 */
static void timer_cb(TimerHandle_t xTimer) {
    uintptr_t period_ms = (uintptr_t)pvTimerGetTimerID(xTimer);
    ESP_LOGI(TAG, "Daemon task: %lu ms timer fired", (unsigned long)period_ms);
}

/**
 * @brief Idle hook function executed by the FreeRTOS Idle Task.
 *
 * This function is called whenever the CPU is idle (no higher-priority
 * tasks are runnable). It toggles an LED approximately every 200 ms
 * while the system remains idle.
 *
 * Implementation details:
 *   - Uses `esp_timer_get_time()` for microsecond resolution timing.
 *   - Avoids blocking calls (e.g., `vTaskDelay` must not be used here).
 *
 * @note Ensure CONFIG_FREERTOS_USE_IDLE_HOOK is enabled in sdkconfig.
 */
void vApplicationIdleHook(void) {
    static int led_level = 0;
    static int64_t last_toggle_us = 0;

    int64_t now = esp_timer_get_time(); // microseconds since boot
    if ((now - last_toggle_us) >= 200000) {
        led_level ^= 1;
        gpio_set_level(LED_GPIO, led_level);
        last_toggle_us = now;
    }
}

/**
 * @brief Application entry point for the FreeRTOS example.
 *
 * This function configures the LED pin and sets up two auto-reload
 * software timers (1s and 3s). Both timers are started and their
 * callbacks are executed by the FreeRTOS Timer Daemon Task.
 *
 * Steps:
 *   1. Configure the LED GPIO for output.
 *   2. Create two periodic timers (1s and 3s).
 *   3. Start both timers so they run in the Daemon Task.
 *
 * @note No user tasks are created. When no other tasks are ready,
 *       the Idle Task runs and toggles the LED via the Idle Hook.
 */
void app_main(void) {
    // Configure LED GPIO
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_GPIO, 0);

    // Create two auto-reload software timers (run in Timer Daemon Task)
    timer_1s = xTimerCreate("T1s",
                            pdMS_TO_TICKS(1000),
                            pdTRUE,                       // auto-reload
                            (void *)((uintptr_t)1000),   // ID: period in ms (for logging)
                            timer_cb);

    timer_3s = xTimerCreate("T3s",
                            pdMS_TO_TICKS(3000),
                            pdTRUE,                       // auto-reload
                            (void *)((uintptr_t)3000),   // ID: period in ms (for logging)
                            timer_cb);

    // Start timers
    if (timer_1s) xTimerStart(timer_1s, 0);
    if (timer_3s) xTimerStart(timer_3s, 0);

    // No user tasks created — Idle Task executes when system is idle
}
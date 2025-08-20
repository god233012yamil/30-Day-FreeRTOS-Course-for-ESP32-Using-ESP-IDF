/**
 * @file blink_two_leds.c
 * @brief ESP-IDF FreeRTOS demo comparing vTaskDelay (relative) vs vTaskDelayUntil (absolute).
 *
 * This example blinks two LEDs at different rates to illustrate timing drift
 * with vTaskDelay() and the stable cadence of vTaskDelayUntil(). It also
 * includes a status task that periodically prints uptime for reference.
 *
 * Wiring:
 *   - LED1 (GPIO2 by default) -> resistor -> GND
 *   - LED2 (GPIO4 by default) -> resistor -> GND
 *
 * Notes:
 *   - Adjust LED1_GPIO / LED2_GPIO to match your hardware.
 *   - Console output includes timestamps to visualize drift behavior.
 */

#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define TAG "DAY7"

// === Adjust these for your board if necessary ===
#ifndef LED1_GPIO
#define LED1_GPIO GPIO_NUM_2   // Often has onboard LED on many DevKit boards
#endif

#ifndef LED2_GPIO
#define LED2_GPIO GPIO_NUM_4
#endif
// ================================================

/**
 * @brief Configure a GPIO as push-pull output and drive it low initially.
 *
 * Sets direction, disables pull-ups/downs and interrupts, and ensures the pin
 * starts in the LOW state. Use this before toggling an LED on the given pin.
 *
 * @param pin GPIO number to configure (e.g., LED1_GPIO or LED2_GPIO).
 */
static inline void configure_led(gpio_num_t pin)
{
    // Configure pin as output and start low
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io);
    gpio_set_level(pin, 0);
}

/**
 * @brief Toggle a GPIO output level.
 *
 * Reads the current level and writes the inverted value, producing a clean
 * square-wave blink when called periodically by a task.
 *
 * @param pin GPIO number to toggle.
 */
static inline void toggle_led(gpio_num_t pin)
{
    int current = gpio_get_level(pin);
    gpio_set_level(pin, !current);
}

/**
 * @brief Task A: blink LED1 using vTaskDelay() (relative delay).
 *
 * Toggles LED1 every 500 ms using a relative delay. Prints the current uptime
 * (ms) and iteration count. Over long runs, loop body time causes accumulated
 * drift versus an ideal schedule.
 *
 * @param pv Unused task parameter.
 */
static void taskA_delay(void *pv)
{
    const TickType_t period_ticks = pdMS_TO_TICKS(500); // 500 ms
    uint32_t iteration = 0;

    ESP_LOGI(TAG, "Task A (vTaskDelay) started on core %d", xPortGetCoreID());

    while (1) {
        toggle_led(LED1_GPIO);
        iteration++;

        // Print a timestamp (ms) to visualize drift
        TickType_t now_ticks = xTaskGetTickCount();
        uint32_t now_ms = now_ticks * portTICK_PERIOD_MS;
        ESP_LOGI(TAG, "[A] t=%" PRIu32 " ms, iter=%" PRIu32, now_ms, iteration);

        // Relative delay: next wake-up occurs 'period_ticks' after this call
        vTaskDelay(period_ticks);
    }
}

/**
 * @brief Task B: blink LED2 using vTaskDelayUntil() (absolute schedule).
 *
 * Toggles LED2 every 1000 ms with a fixed cadence referenced to the initial
 * wake time. Uses vTaskDelayUntil() to minimize drift between iterations.
 *
 * @param pv Unused task parameter.
 */
static void taskB_delay_until(void *pv)
{
    const TickType_t period_ticks = pdMS_TO_TICKS(1000); // 1000 ms
    TickType_t last_wake = xTaskGetTickCount();          // Reference (t0)
    uint32_t iteration = 0;

    ESP_LOGI(TAG, "Task B (vTaskDelayUntil) started on core %d", xPortGetCoreID());

    while (1) {
        toggle_led(LED2_GPIO);
        iteration++;

        TickType_t now_ticks = xTaskGetTickCount();
        uint32_t now_ms = now_ticks * portTICK_PERIOD_MS;
        ESP_LOGI(TAG, "[B] t=%" PRIu32 " ms, iter=%" PRIu32, now_ms, iteration);

        // Absolute delay: wake exactly every 'period_ticks' since last_wake
        vTaskDelayUntil(&last_wake, period_ticks);
    }
}

/**
 * @brief Optional status task that prints uptime periodically.
 *
 * Every 2 seconds, prints an approximate uptime in milliseconds and seconds.
 * This is a didactic utility to correlate observed blink timing with system time.
 *
 * @param pv Unused task parameter.
 */
static void task_status(void *pv)
{
    const TickType_t period_ticks = pdMS_TO_TICKS(2000); // every 2 s
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t seconds = 0;

    ESP_LOGI(TAG, "Status task started on core %d", xPortGetCoreID());

    while (1) {
        vTaskDelayUntil(&last_wake, period_ticks);
        seconds += 2;

        // Report uptime and tick count
        TickType_t ticks = xTaskGetTickCount();
        uint32_t ms = ticks * portTICK_PERIOD_MS;
        ESP_LOGI(TAG, "[STATUS] uptime ~%" PRIu32 " ms (%" PRIu32 "s)", ms, seconds);
    }
}

/**
 * @brief Application entry point: initializes GPIOs and starts tasks.
 *
 * Configures LED GPIOs, then creates Task A (relative delay), Task B (absolute
 * schedule), and an optional status task. Logs an error if task creation fails.
 */
void app_main(void)
{
    // GPIO setup
    configure_led(LED1_GPIO);
    configure_led(LED2_GPIO);

    // Create tasks
    // Priorities kept the same to let the scheduler time-slice fairly
    BaseType_t okA = xTaskCreate(taskA_delay, "TaskA_Delay", 2048, NULL, 5, NULL);
    BaseType_t okB = xTaskCreate(taskB_delay_until, "TaskB_DelayUntil", 2048, NULL, 5, NULL);

    // Optional status task (lower priority)
    xTaskCreate(task_status, "TaskStatus", 2048, NULL, 3, NULL);

    if (okA != pdPASS || okB != pdPASS) {
        ESP_LOGE(TAG, "Failed to create one or more tasks");
    }
}
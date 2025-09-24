/**
 * @file main.c
 * @brief FreeRTOS tick hook demo (ESP-IDF).
 *
 * @details
 * Demonstrates using the FreeRTOS tick hook to notify a task every 500 ms.
 * The tick hook runs in ISR context and uses `vTaskNotifyGiveFromISR` to
 * unblock a consumer task that logs time since boot and toggles an LED.
 *
 * Configuration:
 * - Enable **FreeRTOS Use Tick Hook** (`CONFIG_FREERTOS_USE_TICK_HOOK=y`)
 *   in `menuconfig`.
 * - Adjust `LED_GPIO` for your board if needed.
 *
 * @note The tick hook executes on every system tick and must remain minimal:
 *       no blocking, no logging, only ISR-safe APIs.
 * 
 * @note Ensure "FreeRTOS Use Tick Hook" is enabled in menuconfig (CONFIG_FREERTOS_USE_TICK_HOOK=y).
 */

#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define TAG "TICK_HOOK_DEMO"
#define LED_GPIO GPIO_NUM_2  // adjust for your board

// Task handle that will be notified from the tick hook
static TaskHandle_t s_tick_task = NULL;

// Optional: track approx. ms since boot based on tick period
static volatile uint32_t s_ms_since_boot = 0;

/**
 * @brief FreeRTOS tick hook ISR.
 *
 * @details
 * Invoked by the kernel on every RTOS tick. Accumulates elapsed milliseconds
 * and, every 500 ms, sends a notification to the consumer task using
 * `vTaskNotifyGiveFromISR`. If a higher-priority task is woken, yields from
 * the ISR to run it immediately.
 *
 * @warning
 * Runs in ISR context; use only ISR-safe APIs. Do not block or call logging.
 *
 * @see vTaskNotifyGiveFromISR, portYIELD_FROM_ISR
 */
void IRAM_ATTR vApplicationTickHook(void)
{
    // Accumulate milliseconds using the known tick period
    s_ms_since_boot += portTICK_PERIOD_MS;

    // Notify once every 500 ms
    static uint32_t acc_ms = 0;
    acc_ms += portTICK_PERIOD_MS;

    if (acc_ms >= 500) {
        acc_ms -= 500;

        BaseType_t higher_woken = pdFALSE;
        if (s_tick_task) {
            // Wake the task (one count per half-second)
            vTaskNotifyGiveFromISR(s_tick_task, &higher_woken);
        }
        if (higher_woken) {
            portYIELD_FROM_ISR();
        }
    }
}

/**
 * @brief Task that consumes half-second notifications from the tick hook.
 *
 * @param[in] arg Unused task argument (pass NULL).
 *
 * @details
 * Blocks on `ulTaskNotifyTake` until the tick hook posts a notification
 * (every ~500 ms). On wake, logs the approximate milliseconds since boot and
 * toggles an LED on `LED_GPIO`.
 *
 * @note
 * Safe to perform logging and GPIO operations here (task context).
 */
static void tick_consumer_task(void *arg)
{
    bool led_on = false;

    for (;;) {
        // Block until the tick hook gives us a notification (every 500 ms)
        ulTaskNotifyTake(/*clearOnExit=*/pdTRUE, /*wait=*/portMAX_DELAY);

        // Safe to do work here (task context)
        ESP_LOGI(TAG, "Half-second tick: ~%" PRIu32 " ms since boot", s_ms_since_boot);

        led_on = !led_on;
        gpio_set_level(LED_GPIO, led_on);
    }
}

/**
 * @brief Application entry point.
 *
 * @details
 * Configures the LED GPIO for output, creates the tick consumer task, and
 * logs the current tick configuration (`configTICK_RATE_HZ` and
 * `portTICK_PERIOD_MS`).
 *
 * @post
 * After initialization, the system will toggle the LED and log a message
 * approximately every 500 ms as notifications arrive from the tick hook.
 */
void app_main(void)
{
    // Configure LED
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io);
    gpio_set_level(LED_GPIO, 0);

    // Create the task to be notified by the tick hook
    xTaskCreate(tick_consumer_task, "TickConsumer", 2048, NULL, 6, &s_tick_task);

    ESP_LOGI(TAG, "Tick hook demo started. TICK_RATE_HZ=%d, tick=%d ms",
             configTICK_RATE_HZ, (int)portTICK_PERIOD_MS);
}
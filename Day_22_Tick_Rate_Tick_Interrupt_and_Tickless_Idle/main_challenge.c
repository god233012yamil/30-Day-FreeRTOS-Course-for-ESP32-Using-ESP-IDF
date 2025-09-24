/**
 * @file main.c
 * @brief FreeRTOS tick hook demo (ESP-IDF): dual notification rates.
 *
 * @details
 * Extends the basic tick-hook example by notifying:
 *   - Task A every 100 ms (drives the LED and logs frequently).
 *   - Task B every 1000 ms (logs once per second).
 *
 * The tick hook runs in ISR context. It maintains two independent millisecond
 * accumulators (100 ms / 1000 ms) and uses `vTaskNotifyGiveFromISR` to wake
 * each task at its own cadence. If either task of higher priority is woken,
 * the ISR yields to run it immediately.
 *
 * @note Enable **FreeRTOS Use Tick Hook** (`CONFIG_FREERTOS_USE_TICK_HOOK=y`)
 *       in `menuconfig`. Keep ISR work minimal: no blocking, no logging, only
 *       ISR-safe APIs.
 *
 * @obs
 * With 100 ms notifications, the LED toggles every 100 ms, so it completes a
 * full on/off cycle every 200 ms (~5 Hz). Logging from Task A will also occur
 * at 10 Hz, which is quite chatty. Task B logs at 1 Hz.
 */

#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define TAG "TICK_HOOK_DEMO"
#define LED_GPIO GPIO_NUM_2  // adjust for your board

// Task handles notified from the tick hook
static TaskHandle_t s_task_100ms  = NULL;  // Task A: 100 ms cadence
static TaskHandle_t s_task_1000ms = NULL;  // Task B: 1000 ms cadence

// Approx. ms since boot (based on tick period)
static volatile uint32_t s_ms_since_boot = 0;

/**
 * @brief FreeRTOS tick hook ISR.
 *
 * @details
 * Invoked on every RTOS tick. Updates a boot-time millisecond counter and two
 * cadence accumulators:
 *   - 100 ms accumulator -> notifies Task A.
 *   - 1000 ms accumulator -> notifies Task B.
 *
 * Uses `vTaskNotifyGiveFromISR` for each task independently and performs an
 * ISR yield if a higher-priority task was woken.
 */
void IRAM_ATTR vApplicationTickHook(void)
{
    // Advance "time since boot" by one tick (in ms)
    s_ms_since_boot += portTICK_PERIOD_MS;

    // Two independent cadence accumulators
    static uint32_t acc_100  = 0;
    static uint32_t acc_1000 = 0;

    acc_100  += portTICK_PERIOD_MS;
    acc_1000 += portTICK_PERIOD_MS;

    BaseType_t higher_woken_any = pdFALSE;

    // Notify Task A every 100 ms
    if (acc_100 >= 100) {
        acc_100 -= 100;
        if (s_task_100ms) {
            BaseType_t higher_woken = pdFALSE;
            vTaskNotifyGiveFromISR(s_task_100ms, &higher_woken);
            if (higher_woken) higher_woken_any = pdTRUE;
        }
    }

    // Notify Task B every 1000 ms
    if (acc_1000 >= 1000) {
        acc_1000 -= 1000;
        if (s_task_1000ms) {
            BaseType_t higher_woken = pdFALSE;
            vTaskNotifyGiveFromISR(s_task_1000ms, &higher_woken);
            if (higher_woken) higher_woken_any = pdTRUE;
        }
    }

    if (higher_woken_any) {
        portYIELD_FROM_ISR();
    }
}

/**
 * @brief Task A: consumes 100 ms notifications and toggles the LED.
 *
 * @param[in] arg Unused (pass NULL).
 *
 * @details
 * Blocks on `ulTaskNotifyTake` until the tick hook posts a 100 ms
 * notification. On wake, logs the approximate milliseconds since boot and
 * toggles the LED on `LED_GPIO`.
 *
 * @effects
 * LED toggle period = 100 ms, so full on/off cycle = 200 ms (~5 Hz blink).
 * Logs at ~10 per second.
 */
static void tick_task_100ms(void *arg)
{
    bool led_on = false;

    for (;;) {
        ulTaskNotifyTake(/*clearOnExit=*/pdTRUE, /*wait=*/portMAX_DELAY);

        // Frequent (10 Hz) logging; consider reducing in real apps
        ESP_LOGI(TAG, "[100ms] ~%" PRIu32 " ms since boot", s_ms_since_boot);

        led_on = !led_on;
        gpio_set_level(LED_GPIO, led_on);
    }
}

/**
 * @brief Task B: consumes 1000 ms notifications and logs once per second.
 *
 * @param[in] arg Unused (pass NULL).
 *
 * @details
 * Blocks on `ulTaskNotifyTake` until the tick hook posts a 1000 ms
 * notification. On wake, prints a 1 Hz heartbeat with the current
 * `s_ms_since_boot` value. This task does not touch the LED to avoid races.
 */
static void tick_task_1000ms(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(/*clearOnExit=*/pdTRUE, /*wait=*/portMAX_DELAY);
        ESP_LOGI(TAG, "[1000ms] ~%" PRIu32 " ms since boot (1 Hz heartbeat)", s_ms_since_boot);
    }
}

/**
 * @brief Application entry point.
 *
 * @details
 * Configures the LED GPIO for output, creates the 100 ms and 1000 ms consumer
 * tasks, and logs the tick configuration. After initialization:
 *   - Task A (100 ms) will blink the LED quickly and log often.
 *   - Task B (1000 ms) will log once per second.
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

    // Create tasks with distinct names (priorities can be equal)
    xTaskCreate(tick_task_100ms,  "Tick100ms",  2048, NULL, 6, &s_task_100ms);
    xTaskCreate(tick_task_1000ms, "Tick1000ms", 2048, NULL, 6, &s_task_1000ms);

    ESP_LOGI(TAG, "Tick hook demo started. TICK_RATE_HZ=%d, tick=%d ms",
             configTICK_RATE_HZ, (int)portTICK_PERIOD_MS);
}
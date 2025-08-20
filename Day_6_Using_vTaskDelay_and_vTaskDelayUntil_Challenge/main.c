// https://chatgpt.com/g/g-p-6824806cfccc8191ae159fd4985d30cf-esp-idf/c/689bdafd-bdac-832e-806b-4809c52dc60d

/**
 * @file main.c
 * @brief ESP-IDF demo: compare timing stability of vTaskDelayUntil() (fixed-rate)
 *        vs vTaskDelay() (relative delay).
 *
 * This example creates two FreeRTOS tasks:
 *   1) sensor_sampling_task  — wakes exactly every 200 ms using vTaskDelayUntil()
 *   2) led_blink_task        — blinks an LED every ~1000 ms using vTaskDelay()
 *
 * Use the console logs to observe the measured period jitter for each task.
 * Change LED_GPIO below to match your board's LED (e.g., 2 on many ESP32 dev boards).
 *
 * Build: idf.py build
 * Flash: idf.py flash monitor
 */

#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/gpio.h"

#define TAG                 "TIMING_DEMO"
#define LED_GPIO            GPIO_NUM_2     // Change to your board's LED pin
#define SAMPLING_PERIOD_MS  200            // Fixed-rate period for sensor task
#define BLINK_PERIOD_MS     1000           // Target blink "period" using relative delay

// ------------------------ Forward Declarations ------------------------

static void init_led_gpio(void);
static int  read_fake_sensor(void);
static void sensor_sampling_task(void *arg);
static void led_blink_task(void *arg);

// ------------------------ Helpers ------------------------

/**
 * @brief Initialize LED GPIO as output (active-high).
 *
 * @return void
 */
static void init_led_gpio(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io);
    gpio_set_level(LED_GPIO, 0);
}

/**
 * @brief Simulate a sensor read quickly (non-blocking).
 *
 * This is intentionally fast so timing is dominated by the scheduler,
 * not by I/O. Replace with a real ADC/I2C/SPI read if needed.
 *
 * @return int A pseudo "sensor" value in [0..99].
 */
static int read_fake_sensor(void)
{
    // Use current time as a quick-changing pseudo source
    uint64_t us = esp_timer_get_time();         // microseconds
    return (int)((us / 1000) % 100);            // 0..99
}

// ------------------------ Tasks ------------------------

/**
 * @brief Fixed-rate sensor sampling task using vTaskDelayUntil().
 *
 * Wakes exactly every SAMPLING_PERIOD_MS from a moving absolute deadline
 * anchored by the last wake time (jitter is minimized). We also measure
 * and log the actual loop period using esp_timer_get_time().
 *
 * @param arg Unused.
 * @return void (task never returns).
 */
static void sensor_sampling_task(void *arg)
{
    const TickType_t period_ticks = pdMS_TO_TICKS(SAMPLING_PERIOD_MS);
    TickType_t last_wake = xTaskGetTickCount();   // Anchor reference
    int64_t t_prev_us = esp_timer_get_time();

    ESP_LOGI(TAG, "[sensor] Starting fixed-rate loop at %d ms period", SAMPLING_PERIOD_MS);

    while (1) {
        // Sleep until the next absolute deadline (fixed rate).
        vTaskDelayUntil(&last_wake, period_ticks);

        // Measure actual period
        int64_t now_us = esp_timer_get_time();
        int64_t dt_us  = now_us - t_prev_us;
        t_prev_us = now_us;

        // Do (fast) work after the wakeup to keep schedule tight
        int sample = read_fake_sensor();

        // Log timing — expect dt ~= 200 ms with small jitter
        ESP_LOGI(TAG,
                 "[sensor] sample=%d  period=%.2f ms  (ticks=%" PRIu32 ")",
                 sample,
                 (double)dt_us / 1000.0,
                 (uint32_t)period_ticks);
    }
}

/**
 * @brief Relative-delay LED blink task using vTaskDelay().
 *
 * Sleeps for BLINK_PERIOD_MS *after* each iteration. Any work done before
 * the delay pushes the next wake later, so drift and jitter accumulate more
 * readily compared to vTaskDelayUntil().
 *
 * @param arg Unused.
 * @return void (task never returns).
 */
static void led_blink_task(void *arg)
{
    bool level = false;
    int64_t t_prev_us = esp_timer_get_time();

    ESP_LOGI(TAG, "[blink] Starting relative-delay loop at ~%d ms period", BLINK_PERIOD_MS);

    while (1) {
        // Do work first, then delay relatively
        level = !level;
        gpio_set_level(LED_GPIO, level);

        int64_t now_us = esp_timer_get_time();
        int64_t dt_us  = now_us - t_prev_us;
        t_prev_us = now_us;

        // Log timing — expect more variation than the sensor task
        ESP_LOGI(TAG,
                 "[blink] LED=%d  period=%.2f ms",
                 level ? 1 : 0,
                 (double)dt_us / 1000.0);

        // Relative delay (accumulates drift if work varies)
        vTaskDelay(pdMS_TO_TICKS(BLINK_PERIOD_MS));
    }
}

// ------------------------ Entry Point ------------------------

/**
 * @brief Application entry point: configure GPIO and start both tasks.
 *
 * Creates:
 *   - sensor_sampling_task (higher priority): fixed-rate 200 ms period
 *   - led_blink_task       (lower  priority): relative 1000 ms delay
 *
 * @return void
 */
void app_main(void)
{
    ESP_LOGI(TAG, "Initializing...");
    init_led_gpio();

    // Create the fixed-rate sensor task (higher priority to reduce preemption jitter)
    BaseType_t ok1 = xTaskCreate(
        sensor_sampling_task,
        "sensor_sampling_task",
        4096,
        NULL,
        5,              // Priority (tune as needed)
        NULL
    );

    // Create the relative-delay LED task
    BaseType_t ok2 = xTaskCreate(
        led_blink_task,
        "led_blink_task",
        3072,
        NULL,
        3,              // Lower than sensor task
        NULL
    );

    if (ok1 != pdPASS || ok2 != pdPASS) {
        ESP_LOGE(TAG, "Failed to create tasks (sensor=%ld, blink=%ld)", (long)ok1, (long)ok2);
    } else {
        ESP_LOGI(TAG, "Tasks started. Watch the periods in the log.");
    }
}
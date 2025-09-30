/**
 * @file
 * @brief Power Management with FreeRTOS on ESP32 — Light Sleep + Timer Wakeup
 *
 * Overview:
 *   Demonstrates how to use ESP-IDF power management APIs together with
 *   FreeRTOS to periodically enter Light Sleep and wake via a timer.
 *   A simulated sensor task runs under FreeRTOS; the main loop repeatedly
 *   sleeps and wakes every 5 seconds using the RTC timer wake source.
 *
 * What this example shows:
 *   1) Enabling timer wakeup with `esp_sleep_enable_timer_wakeup`.
 *   2) Entering light sleep using `esp_light_sleep_start`.
 *   3) Running a FreeRTOS task that simulates sensor work using delays.
 *   4) Basic logging around sleep/wake transitions.
 *
 * Hardware/SDK requirements:
 *   - ESP32 (any ESP-IDF-supported target with Light Sleep capability)
 *   - ESP-IDF v4.x+ (or newer)
 *
 * Build/Flash/Monitor (example):
 *   idf.py set-target esp32
 *   idf.py build flash monitor
 *
 * Power notes:
 *   - Light Sleep reduces power consumption while keeping RAM and most
 *     peripherals powered; CPU is paused until wakeup.
 *   - Choose an appropriate wake period; this example uses 5 seconds.
 *   - Consider pausing unnecessary peripherals before light sleep for
 *     best results.
 *
 * Args:
 *   (None)
 *
 * Returns:
 *   (None)
 *
 * Example:
 *   - Observe logs: the system announces entering light sleep, then wakes
 *     after ~5 s. The sensor task continues to log every 2 s.
 *
 * Limitations:
 *   - This is a minimal example. In production, handle all error returns
 *     and coordinate sleep with active peripherals and tasks.
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include "esp_err.h"

#define TAG "DAY24_PM"
#define SENSOR_TASK_STACK   (2048)
#define SENSOR_TASK_PRIO    (5)
#define SENSOR_PERIOD_MS    (2000)
#define WAKE_PERIOD_US      (5000000ULL)  // 5 seconds

/**
 * Simulated sensor reading task.
 *
 * Periodically simulates a sensor read (via logging) and then delays to
 * emulate processing time or sampling interval.
 *
 * Args:
 *   pvParameter: Unused task argument (pass NULL).
 *
 * Returns:
 *   This function never returns; it loops forever.
 *
 * Notes:
 *   - Replace the logging call with real sensor I/O as needed.
 *   - The delay uses `pdMS_TO_TICKS(SENSOR_PERIOD_MS)` to be RTOS-tick-safe.
 */
static void sensor_task(void *pvParameter) {
    (void)pvParameter;
    while (1) {
        ESP_LOGI(TAG, "Reading sensor...");
        vTaskDelay(pdMS_TO_TICKS(SENSOR_PERIOD_MS));
    }
}

/**
 * Main application entry point.
 *
 * Configures a timer wakeup source, creates the sensor task, and then enters a
 * loop that repeatedly goes into Light Sleep and wakes after the configured
 * period. Wakeups are logged for visibility.
 *
 * Args:
 *   (None)
 *
 * Returns:
 *   (None)
 *
 * Example:
 *   - On boot, a sensor task starts logging every 2 seconds.
 *   - The main task announces Light Sleep, sleeps for ~5 seconds, wakes,
 *     and logs the wakeup cause before sleeping again.
 *
 * Notes:
 *   - If you add other wake sources (GPIO, ULP, etc.), check and log the cause
 *     via `esp_sleep_get_wakeup_cause()` as demonstrated.
 *   - Always verify return values of ESP-IDF APIs in production code.
 */
void app_main(void) {
    // Enable timer wakeup (5 seconds). In production, check the return value.
    esp_err_t err = esp_sleep_enable_timer_wakeup(WAKE_PERIOD_US);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable timer wakeup: %s", esp_err_to_name(err));
    }

    // Create the simulated sensor task.
    BaseType_t created = xTaskCreate(sensor_task, "SensorTask",
                                     SENSOR_TASK_STACK, NULL, SENSOR_TASK_PRIO, NULL);
    if (created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create SensorTask");
    }

    // Periodically enter Light Sleep and wake via RTC timer.
    while (1) {
        ESP_LOGI(TAG, "Entering Light Sleep for ~%llu us", (unsigned long long)WAKE_PERIOD_US);

        // Start Light Sleep; returns after wakeup.
        err = esp_light_sleep_start();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_light_sleep_start failed: %s", esp_err_to_name(err));
        }

        // Log wakeup cause for clarity (timer expected).
        esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
        switch (cause) {
            case ESP_SLEEP_WAKEUP_TIMER:
                ESP_LOGI(TAG, "Woke up: RTC timer");
                break;
            case ESP_SLEEP_WAKEUP_UNDEFINED:
                ESP_LOGW(TAG, "Woke up: cause undefined");
                break;
            default:
                ESP_LOGI(TAG, "Woke up: cause=%d", (int)cause);
                break;
        }
    }
}
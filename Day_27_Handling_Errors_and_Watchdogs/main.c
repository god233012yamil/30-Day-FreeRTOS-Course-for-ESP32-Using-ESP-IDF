/**
 * @file        main.c
 * @author      Yamil Garcia
 *
 * @brief       Demonstration of the Task Watchdog Timer (WDT) in FreeRTOS on an ESP32.
 *
 * This example creates two tasks:
 *  - A "healthy task" that regularly feeds the watchdog.
 *  - A "stuck task" that simulates a deadlock by never feeding the watchdog.
 *
 * The Task WDT is configured with a 5-second timeout. If a monitored task fails to reset
 * the watchdog within this period, the system will trigger a panic/reset (depending on
 * configuration). This project shows how the watchdog protects the system from tasks
 * that become unresponsive.
 *
 * Requirements:
 *  - Enable stack overflow check in menuconfig.
 *  - Ensure Task WDT is properly configured in ESP-IDF menuconfig or in code.
 *
 * @version     0.2
 * @date        2025-10-02
 * @copyright   Copyright (c) 2025
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
#include "esp_log.h"

#define TAG "DAY27"

/**
 * @brief Healthy task that simulates normal operation.
 *
 * This task demonstrates how a well-behaved task should periodically feed
 * the Task Watchdog Timer (WDT) to signal that it is responsive. It is added
 * to the watchdog's list of monitored tasks and calls `esp_task_wdt_reset()`
 * once per second.
 *
 * @param pvParameter Unused task parameter.
 */
void healthy_task(void *pvParameter) {
    esp_task_wdt_add(NULL);  // Add this task to the WDT

    while (1) {
        ESP_LOGI(TAG, "Healthy task running, feeding WDT");
        esp_task_wdt_reset();  // Feed the WDT
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/**
 * @brief Stuck task that simulates a deadlock.
 *
 * This task is deliberately written to never feed the Task Watchdog Timer (WDT).
 * It demonstrates what happens when a monitored task becomes unresponsive.
 * Eventually, the WDT will trigger a panic/reset due to the timeout.
 *
 * @param pvParameter Unused task parameter.
 */
void stuck_task(void *pvParameter) {
    esp_task_wdt_add(NULL);  // Add this task to the WDT

    ESP_LOGI(TAG, "Stuck task will block forever...");
    while (1) {
        // Simulate deadlock (never resets WDT)
    }
}

/**
 * @brief Main application entry point.
 *
 * Configures and initializes the Task Watchdog Timer (WDT) with:
 *  - Timeout: 5 seconds
 *  - Panic on timeout
 *  - Monitoring idle tasks on all cores
 *
 * The function also adds the `app_main` task itself to the watchdog's list
 * of monitored tasks. It then creates two tasks:
 *  - `healthy_task`: Regularly feeds the WDT.
 *  - `stuck_task`: Never feeds the WDT to simulate a deadlock.
 *
 * If the stuck task fails to reset the watchdog within the timeout, the WDT
 * will trigger a panic/reset to protect the system.
 */
void app_main() {
    static const esp_task_wdt_config_t twdt_cfg = {
        .timeout_ms = 5000,                              // 5 seconds
        .trigger_panic = true,                           // panic on timeout
        .idle_core_mask = (1 << portNUM_PROCESSORS) - 1, // monitor idle tasks
    };

    // Initialize TWDT safely (skip if already done by startup)
    esp_err_t err = esp_task_wdt_init(&twdt_cfg);
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "TWDT already initialized at boot; skipping init.");
    } else {
        ESP_ERROR_CHECK(err);
    }

    // Add app_main itself to be monitored
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    // Create demo tasks
    xTaskCreate(healthy_task, "HealthyTask", 2048, NULL, 5, NULL);
    xTaskCreate(stuck_task, "StuckTask", 2048, NULL, 5, NULL);
}
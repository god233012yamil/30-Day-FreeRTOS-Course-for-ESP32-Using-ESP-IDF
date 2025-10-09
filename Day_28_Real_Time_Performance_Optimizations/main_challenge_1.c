#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

/**
 * @file test.c
 * @brief Day 28 – Performance optimizations in FreeRTOS tasks on ESP32 (ESP-IDF):
 *        replacing a queue with direct-to-task notifications and comparing CPU usage.
 *
 * This example refactors a producer/consumer pipeline to use direct-to-task
 * notifications instead of a FreeRTOS queue. Task notifications are a
 * zero-allocation, low-overhead primitive that can be used as:
 *   - A lightweight event flag (binary semaphore replacement),
 *   - A counting semaphore, or
 *   - A 32-bit value delivery mechanism.
 *
 * In this demo, the producer delivers a monotonically increasing 32-bit value
 * to the consumer using xTaskNotify() with eSetValueWithOverwrite. The consumer
 * blocks on xTaskNotifyWait() to receive the latest value without polling.
 *
 * Key ideas demonstrated:
 *  - Replacing queue send/receive with task notifications to reduce context
 *    and memory overhead.
 *  - Blocking wait on notifications to avoid busy-wait and wasted CPU cycles.
 *  - Periodic runtime statistics reporting via vTaskGetRunTimeStats().
 *
 * Build/Config prerequisites:
 *  - Enable runtime stats in Kconfig: CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y
 *    and CONFIG_FREERTOS_USE_TRACE_FACILITY=y.
 *
 * How to compare CPU usage vs. the queue-based version:
 *  1) Build and run the original (queue) example; capture vTaskGetRunTimeStats().
 *  2) Build and run this (notification) example; capture the same stats.
 *  3) Compare the cumulative run time / %CPU of Producer, Consumer, and Idle.
 *     You should observe slightly lower overhead and more Idle time here.
 */

#define TAG "DAY28"

/* Forward declaration to allow the producer to notify the consumer. */
static TaskHandle_t s_consumer_handle = NULL;

/**
 * producer_task
 *
 * Generates a monotonically increasing integer at 10 Hz and notifies the
 * consumer task with the latest value using xTaskNotify() and the action
 * eSetValueWithOverwrite (so the most recent value wins if the consumer is
 * briefly busy).
 *
 * Args:
 *   pvParameters: (void*) Unused; must be NULL.
 *
 * Behavior:
 *   - Increments a 32-bit counter each iteration.
 *   - Calls xTaskNotify() to deliver the counter to the consumer.
 *   - Sleeps for 100 ms between iterations (10 Hz producer).
 */
static void producer_task(void *pvParameters) {
    (void)pvParameters;
    uint32_t count = 0;
    while (1) {
        count++;
        /* Deliver the value to the consumer; overwrite any pending value. */
        if (s_consumer_handle) {
            (void)xTaskNotify(s_consumer_handle, count, eSetValueWithOverwrite);
        }
        vTaskDelay(pdMS_TO_TICKS(100)); /* 10 Hz */
    }
}

/**
 * consumer_task
 *
 * Blocks on xTaskNotifyWait() to receive the latest 32-bit value sent by the
 * producer via a direct-to-task notification. This avoids active polling and
 * reduces CPU usage versus a busy loop.
 *
 * Args:
 *   pvParameters: (void*) Unused; must be NULL.
 *
 * Notes:
 *   - ulNotifiedValue receives the value provided by the producer.
 *   - Clear-on-exit mask uses ULONG_MAX to clear all bits/values.
 */
static void consumer_task(void *pvParameters) {
    (void)pvParameters;
    uint32_t received = 0;
    for (;;) {
        /*
         * Wait indefinitely for a notification carrying a 32-bit value.
         * Clear all notification bits/values on exit to avoid stale data.
         */
        (void)xTaskNotifyWait(/* ulBitsToClearOnEntry   */ 0,
                              /* ulBitsToClearOnExit    */ ULONG_MAX,
                              /* pulNotificationValue   */ &received,
                              /* xTicksToWait           */ portMAX_DELAY);
        ESP_LOGI(TAG, "Processed value (notify): %" PRIu32, received);
    }
}

/**
 * stats_task
 *
 * Periodically prints runtime statistics for all tasks using
 * vTaskGetRunTimeStats(). This helps validate that optimizations (priorities,
 * notification mechanism) behave as expected.
 *
 * Args:
 *   pvParameters: (void*) Unused; must be NULL.
 *
 * Requirements:
 *   - CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS must be enabled.
 *   - CONFIG_FREERTOS_USE_TRACE_FACILITY must be enabled.
 *
 * Output:
 *   Logs a formatted table of cumulative run times and CPU percentages.
 */
static void stats_task(void *pvParameters) {
    (void)pvParameters;
    char buffer[256];
    while (1) {
        vTaskGetRunTimeStats(buffer);
        ESP_LOGI(TAG, "Runtime Stats (notifications):%s", buffer);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

/**
 * app_main
 *
 * Application entry point. Spawns the producer, consumer, and statistics tasks
 * with tuned stack sizes and priorities suitable for a low-latency pipeline.
 *
 * Design:
 *   - Priorities: Consumer(5) > Producer(4) > Stats(3) to prioritize latency.
 *   - Notification path: producer -> consumer via xTaskNotify/eSetValueWithOverwrite.
 *
 * Returns:
 *   void
 */
void app_main(void) {
    /* Create tasks with tuned priorities and modest stack sizes. */
    xTaskCreate(consumer_task, "Consumer", 2048, NULL, 5, &s_consumer_handle);
    xTaskCreate(producer_task, "Producer", 2048, NULL, 4, NULL);
    xTaskCreate(stats_task,    "Stats",    4096, NULL, 3, NULL);

    ESP_LOGI(TAG, "Day 28: Using task notifications instead of a queue");
}
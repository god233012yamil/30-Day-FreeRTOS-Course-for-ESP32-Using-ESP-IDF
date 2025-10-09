/**
 * @file test.c
 * @brief Day 28 – Performance optimizations in FreeRTOS tasks on ESP32 (ESP-IDF).
 *
 * This example demonstrates a lightweight producer/consumer pipeline using
 * FreeRTOS primitives and a periodic statistics task to observe CPU usage.
 * The code favors low-latency interactions and clear priority separation.
 *
 * Key ideas demonstrated:
 *  - Minimal-copy communication using a queue of fixed-size elements (int).
 *  - Producer with zero block time to avoid priority inversion.
 *  - Consumer that blocks on the queue to avoid polling and wasted cycles.
 *  - Periodic runtime statistics reporting via vTaskGetRunTimeStats().
 *
 * Build/Config prerequisites:
 *  - Enable runtime stats in Kconfig: CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y
 *    and provide a timing source (ESP-IDF typically wires this automatically
 *    when the option is enabled). Also enable CONFIG_FREERTOS_USE_TRACE_FACILITY.
 *  - Logging: ensure ESP_LOG level for this tag is visible (default is OK).
 *
 * Tuning notes:
 *  - Queue length and element size are deliberately small to reduce memory.
 *  - Task priorities are chosen so that the consumer preempts the producer
 *    (consumer > producer) to minimize queue buildup.
 *  - Consider replacing the queue with direct-to-task notifications for a
 *    single-consumer design to further reduce overhead (not shown here for
 *    clarity).
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#define TAG "DAY28"

/** Global queue used by producer and consumer. */
static QueueHandle_t data_queue;

/**
 * Producer task.
 *
 * Generates a monotonically increasing integer at 10 Hz and attempts to send
 * it to the queue without blocking. If the queue is full, the value is simply
 * skipped to keep the producer real-time friendly.
 *
 * Args:
 *   pvParameters: (void*) Unused; must be NULL.
 *
 * Behavior:
 *   - Increments an internal counter each iteration.
 *   - Calls xQueueSend() with a block time of 0 ticks.
 *   - Sleeps for 100 ms between iterations.
 */
static void producer_task(void *pvParameters) {
    (void)pvParameters;
    int count = 0;
    while (1) {
        count++;
        /* Send integer to queue with zero block time to avoid priority inversion. */
        (void)xQueueSend(data_queue, &count, 0);
        vTaskDelay(pdMS_TO_TICKS(100)); /* 10 Hz producer */
    }
}

/**
 * Consumer task.
 *
 * Blocks on the queue waiting for new integers produced by the producer. When
 * a value arrives, it logs the value. This avoids active polling and conserves
 * CPU cycles.
 *
 * Args:
 *   pvParameters: (void*) Unused; must be NULL.
 *
 * Notes:
 *   If the design guarantees a single producer and single consumer, replacing
 *   the queue with direct task notifications can further reduce context and
 *   memory overhead.
 */
static void consumer_task(void *pvParameters) {
    (void)pvParameters;
    int value = 0;
    while (1) {
        if (xQueueReceive(data_queue, &value, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "Processed value: %d", value);
        }
    }
}

/**
 * Statistics task.
 *
 * Periodically prints runtime statistics for all tasks using
 * vTaskGetRunTimeStats(). This helps validate that optimizations (priorities,
 * queue lengths, notification mechanisms) behave as expected.
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
        ESP_LOGI(TAG, "Runtime Stats:\n%s", buffer);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

/**
 * Application entry point.
 *
 * Creates the inter-task queue and spawns the producer, consumer, and
 * statistics tasks with tuned stack sizes and priorities.
 *
 * Design:
 *   - Queue depth: 10 elements of type int.
 *   - Priorities: Consumer(5) > Producer(4) > Stats(3) to prioritize latency.
 *
 * Returns:
 *   void
 */
void app_main(void) {
    /* Create queue (optimized for integers). */
    data_queue = xQueueCreate(10, sizeof(int));

    /* Create tasks with tuned priorities and modest stack sizes. */
    xTaskCreate(producer_task, "Producer", 2048, NULL, 4, NULL);
    xTaskCreate(consumer_task, "Consumer", 2048, NULL, 5, NULL);
    xTaskCreate(stats_task,    "Stats",    4096, NULL, 3, NULL);

    ESP_LOGI(TAG, "Day 28 Performance Optimizations started");
}
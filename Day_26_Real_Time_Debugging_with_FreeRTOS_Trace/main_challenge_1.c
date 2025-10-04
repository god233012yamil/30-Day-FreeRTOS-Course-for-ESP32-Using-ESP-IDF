/**
 * @file main.c
 * @brief FreeRTOS Runtime Stats + Inter-Task Exchange via Queue (ESP32, ESP-IDF)
 *
 * This example demonstrates collecting and displaying runtime statistics of
 * FreeRTOS tasks with `vTaskGetRunTimeStats()` while two tasks exchange
 * sequential integers through a **FreeRTOS queue**.
 *
 * Tasks:
 * - Producer: generates a monotonically increasing integer and sends it to a queue.
 * - Consumer: receives integers from the queue and performs simulated work.
 * - StatsTask: prints per-task CPU usage periodically.
 *
 * Why a queue?
 * - Queues provide safe, bounded, FIFO communication and natural blocking semantics.
 * - Blocking on send/receive allows the idle tasks to run, feeding the Task WDT.
 *
 * Menuconfig requirements (ESP-IDF):
 * - Component config → FreeRTOS → Enable run-time stats collection (ON)
 * - Use esp_timer for run-time stats (ON)
 *
 * Build/Run:
 *   idf.py build flash monitor
 *
 * Notes:
 * - Small "spin" loops are included to make CPU usage differences visible in stats
 *   while still yielding regularly. The demo is safe for the watchdog.
 */

#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#define TAG "STATS_DEMO"

// ===== Tunables to make stats visible without tripping the WDT =====
#define DEMO_SEND_PERIOD_MS   10      ///< Period between produced values.
#define DEMO_SPIN_COUNT_TX    25000   ///< Simulated work in producer.
#define DEMO_SPIN_COUNT_RX    25000   ///< Simulated work in consumer.
#define QUEUE_LEN             16      ///< Queue depth for integer messages.

// ===== Task handles / objects =====
static TaskHandle_t s_producer_handle = NULL;
static TaskHandle_t s_consumer_handle = NULL;
static QueueHandle_t s_queue = NULL;

/**
 * @brief Lightweight busy loop to simulate CPU work.
 *
 * Burns a configurable number of cycles so runtime statistics show clearer task
 * contributions. The loop is deterministic and does not touch memory beyond a
 * single volatile counter.
 *
 * @param count Number of loop iterations to execute.
 */
static inline void spin_work(volatile uint32_t count)
{
    while (count--) {
        __asm__ __volatile__("nop");
    }
}

/**
 * @brief Producer task that generates sequential integers and sends them to the queue.
 *
 * The producer performs a small amount of simulated work (spin) and then attempts
 * to send the current value to the queue. If the queue is full, it waits up to
 * one tick before dropping the sample (non-fatal for this demo). After sending,
 * it delays for @ref DEMO_SEND_PERIOD_MS to yield the CPU.
 *
 * @param pvParameters Unused.
 */
static void producer_task(void *pvParameters)
{
    (void)pvParameters;
    uint32_t value = 0;

    ESP_LOGI(TAG, "Producer started (QUEUE, period=%d ms)", DEMO_SEND_PERIOD_MS);

    for (;;) {
        // Simulate some producer-side computation
        spin_work(DEMO_SPIN_COUNT_TX);

        // Send value to the queue; wait briefly if full
        if (xQueueSend(s_queue, &value, 1) != pdPASS) {
            // Optional: record a drop; in real apps consider backpressure handling
            // ESP_LOGW(TAG, "Queue full, drop value=%" PRIu32, value);
        }

        value++;
        vTaskDelay(pdMS_TO_TICKS(DEMO_SEND_PERIOD_MS));
    }
}

/**
 * @brief Consumer task that receives integers from the queue and "processes" them.
 *
 * The consumer blocks on the queue indefinitely until data is available, then
 * performs simulated work on the received item. Blocking is efficient and ensures
 * the idle task runs when there is no work, which keeps the Task WDT satisfied.
 *
 * @param pvParameters Unused.
 */
static void consumer_task(void *pvParameters)
{
    (void)pvParameters;
    uint32_t rx = 0;
    uint64_t sum = 0; // dummy accumulator to mimic some useful computation

    ESP_LOGI(TAG, "Consumer started (QUEUE, blocks on receive)");

    for (;;) {
        // Wait for the next item; block forever until producer provides one
        if (xQueueReceive(s_queue, &rx, portMAX_DELAY) == pdPASS) {
            // Simulate consumer-side processing
            sum += rx;
            spin_work(DEMO_SPIN_COUNT_RX);
        }

        // Yield at least one tick; not strictly necessary since we block above,
        // but it smooths scheduling if processing bursts occur.
        vTaskDelay(1);
        (void)sum; // keep the optimizer from removing the accumulator
    }
}

/**
 * @brief Periodic statistics printer task.
 *
 * Uses `vTaskGetRunTimeStats()` to collect per-task CPU usage and logs a table
 * every 5 seconds. The buffer must be large enough to hold all task rows.
 *
 * @param pvParameters Unused.
 */
static void stats_task(void *pvParameters)
{
    (void)pvParameters;
    char buffer[1024];

    for (;;) {
        ESP_LOGI(TAG, "----- Runtime Stats (time, %% CPU) -----");
        vTaskGetRunTimeStats(buffer);
        ESP_LOGI(TAG, "\n%s", buffer);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

/**
 * @brief Application entry point.
 *
 * Initializes the queue and creates three tasks:
 * - Producer (priority 5, pinned to core 0)
 * - Consumer (priority 5, pinned to core 0)
 * - StatsTask (priority 3, not pinned)
 *
 * Tasks either block on queue operations or delay periodically; this guarantees
 * that the FreeRTOS idle tasks run and the Task WDT remains fed.
 */
void app_main(void)
{
    // Create the queue that transports 32-bit integers
    s_queue = xQueueCreate(QUEUE_LEN, sizeof(uint32_t));
    if (s_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create queue (len=%d)", QUEUE_LEN);
        return;
    }

    // Create producer/consumer with symmetric priorities, pinned to the same core
    // to highlight CPU usage in a single-core view; adjust as desired.
    xTaskCreatePinnedToCore(producer_task, "Producer",  3072, NULL, 5, &s_producer_handle, 0);
    xTaskCreatePinnedToCore(consumer_task, "Consumer",  3072, NULL, 5, &s_consumer_handle, 0);

    // Stats printer (lower priority) — no need to pin
    xTaskCreate(stats_task, "StatsTask", 4096, NULL, 3, NULL);

    ESP_LOGI(TAG, "Inter-task queue demo started (queue length=%d)", QUEUE_LEN);
}
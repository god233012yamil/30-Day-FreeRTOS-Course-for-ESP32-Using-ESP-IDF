/**
 * @file main.c
 * @brief FreeRTOS Runtime Stats + Inter-Task Queue + Stack Monitoring (ESP32, ESP-IDF)
 *
 * This example demonstrates:
 * 1) Collecting and displaying runtime statistics of FreeRTOS tasks via
 *    `vTaskGetRunTimeStats()`.
 * 2) Exchanging integers between a Producer and Consumer using a FreeRTOS queue.
 * 3) Monitoring stack usage using `uxTaskGetStackHighWaterMark()` for each task.
 *
 * Tasks:
 * - Producer: generates monotonically increasing integers and sends them to a queue.
 * - Consumer: receives integers from the queue and simulates processing work.
 * - StatsTask: prints per-task CPU usage periodically.
 * - StackMonTask: prints each task's stack high-water mark (headroom) periodically.
 *
 * Menuconfig requirements (ESP-IDF):
 * - Component config → FreeRTOS → Enable run-time stats collection (ON)
 * - Use esp_timer for run-time stats (ON)
 *
 * Notes:
 * - The high-water mark is the **minimum ever remaining** stack (in words) since the task started.
 *   Lower values indicate heavier stack usage. If this approaches zero, you risk a stack overflow.
 * - Stack depths passed to xTaskCreate* are in **words**, not bytes. On ESP32, 1 word = 4 bytes.
 * - Tasks either block on queue ops or delay periodically so the Idle tasks run and the Task WDT is fed.
 */

#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#define TAG "STATS_DEMO"

// ===== Tunables to make runtime/stack differences visible without tripping the WDT =====
#define DEMO_SEND_PERIOD_MS     10      ///< Producer period (ms).
#define DEMO_SPIN_COUNT_TX      25000   ///< Simulated work in producer.
#define DEMO_SPIN_COUNT_RX      25000   ///< Simulated work in consumer.
#define QUEUE_LEN               16      ///< Queue depth (items).

// ===== Task stack depths (in words). 1 word = 4 bytes on ESP32 =====
#define PRODUCER_STACK_WORDS    3072    ///< ~12 KB
#define CONSUMER_STACK_WORDS    3072    ///< ~12 KB
#define STATS_STACK_WORDS       4096    ///< ~16 KB
#define STACKMON_STACK_WORDS    3072    ///< ~12 KB

// ===== Task priorities =====
#define PRODUCER_PRIO           5
#define CONSUMER_PRIO           5
#define STATS_PRIO              3
#define STACKMON_PRIO           3

// ===== Task handles / objects =====
static TaskHandle_t s_producer_handle = NULL;
static TaskHandle_t s_consumer_handle = NULL;
static TaskHandle_t s_stats_handle    = NULL;
static TaskHandle_t s_stackmon_handle = NULL;
static QueueHandle_t s_queue          = NULL;

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
 * @brief Stack monitoring task: prints high-water mark for each task periodically.
 *
 * This task samples the high-water mark (minimum ever remaining stack) for each
 * application task and prints it alongside the configured stack depth, in both
 * words and bytes, plus an estimated utilization percentage:
 *
 *     utilization_% ≈ 100 * (1 - high_water_words / configured_depth_words)
 *
 * If the high-water mark approaches zero, you risk a stack overflow and should
 * increase the stack depth for that task or reduce its peak stack usage.
 *
 * @param pvParameters Unused.
 */
static void stack_monitor_task(void *pvParameters)
{
    (void)pvParameters;

    const TickType_t period = pdMS_TO_TICKS(5000);

    for (;;) {
        UBaseType_t hw_prod   = uxTaskGetStackHighWaterMark(s_producer_handle);
        UBaseType_t hw_cons   = uxTaskGetStackHighWaterMark(s_consumer_handle);
        UBaseType_t hw_stats  = uxTaskGetStackHighWaterMark(s_stats_handle);
        UBaseType_t hw_stackm = uxTaskGetStackHighWaterMark(NULL); // self

        // Convert words→bytes
        size_t hw_prod_b   = hw_prod   * sizeof(StackType_t);
        size_t hw_cons_b   = hw_cons   * sizeof(StackType_t);
        size_t hw_stats_b  = hw_stats  * sizeof(StackType_t);
        size_t hw_stackm_b = hw_stackm * sizeof(StackType_t);

        // Estimated utilization %
        float util_prod   = 100.0f * (1.0f - ((float)hw_prod   / (float)PRODUCER_STACK_WORDS));
        float util_cons   = 100.0f * (1.0f - ((float)hw_cons   / (float)CONSUMER_STACK_WORDS));
        float util_stats  = 100.0f * (1.0f - ((float)hw_stats  / (float)STATS_STACK_WORDS));
        float util_stackm = 100.0f * (1.0f - ((float)hw_stackm / (float)STACKMON_STACK_WORDS));

        ESP_LOGI(TAG, "----- Stack High-Water Mark (HWM) -----");
        ESP_LOGI(TAG, "Producer:   HWM=%5u words (%5u bytes) | Depth=%5u words (%5u bytes) | ~%2.1f%% used",
                 (unsigned)hw_prod,   (unsigned)hw_prod_b,
                 (unsigned)PRODUCER_STACK_WORDS, (unsigned)(PRODUCER_STACK_WORDS * sizeof(StackType_t)),
                 util_prod);
        ESP_LOGI(TAG, "Consumer:   HWM=%5u words (%5u bytes) | Depth=%5u words (%5u bytes) | ~%2.1f%% used",
                 (unsigned)hw_cons,   (unsigned)hw_cons_b,
                 (unsigned)CONSUMER_STACK_WORDS, (unsigned)(CONSUMER_STACK_WORDS * sizeof(StackType_t)),
                 util_cons);
        ESP_LOGI(TAG, "StatsTask:  HWM=%5u words (%5u bytes) | Depth=%5u words (%5u bytes) | ~%2.1f%% used",
                 (unsigned)hw_stats,  (unsigned)hw_stats_b,
                 (unsigned)STATS_STACK_WORDS, (unsigned)(STATS_STACK_WORDS * sizeof(StackType_t)),
                 util_stats);
        ESP_LOGI(TAG, "StackMon:   HWM=%5u words (%5u bytes) | Depth=%5u words (%5u bytes) | ~%2.1f%% used",
                 (unsigned)hw_stackm, (unsigned)hw_stackm_b,
                 (unsigned)STACKMON_STACK_WORDS, (unsigned)(STACKMON_STACK_WORDS * sizeof(StackType_t)),
                 util_stackm);

        // (Optional) Idle tasks per core (ESP32 is dual-core). Uncomment if desired:
        // TaskHandle_t idle0 = xTaskGetIdleTaskHandleForCPU(0);
        // TaskHandle_t idle1 = xTaskGetIdleTaskHandleForCPU(1);
        // ESP_LOGI(TAG, "IDLE0 HWM=%u words, IDLE1 HWM=%u words",
        //          (unsigned)uxTaskGetStackHighWaterMark(idle0),
        //          (unsigned)uxTaskGetStackHighWaterMark(idle1));

        vTaskDelay(period);
    }
}

/**
 * @brief Application entry point.
 *
 * Initializes the queue and creates four tasks:
 * - Producer (priority 5, pinned to core 0)
 * - Consumer (priority 5, pinned to core 0)
 * - StatsTask (priority 3, not pinned)
 * - StackMonTask (priority 3, not pinned)
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
    xTaskCreatePinnedToCore(producer_task, "Producer",
                            PRODUCER_STACK_WORDS, NULL, PRODUCER_PRIO, &s_producer_handle, 0);

    xTaskCreatePinnedToCore(consumer_task, "Consumer",
                            CONSUMER_STACK_WORDS, NULL, CONSUMER_PRIO, &s_consumer_handle, 0);

    // Stats printer (lower priority) — no need to pin
    xTaskCreate(stats_task, "StatsTask",
                STATS_STACK_WORDS, NULL, STATS_PRIO, &s_stats_handle);

    // Stack monitor (lower priority) — no need to pin
    xTaskCreate(stack_monitor_task, "StackMonTask",
                STACKMON_STACK_WORDS, NULL, STACKMON_PRIO, &s_stackmon_handle);

    ESP_LOGI(TAG, "Inter-task queue + stack monitoring demo started (queue length=%d)", QUEUE_LEN);
}
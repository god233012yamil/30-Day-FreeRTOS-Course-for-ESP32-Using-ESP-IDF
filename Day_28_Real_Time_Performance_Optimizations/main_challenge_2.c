/**
 * @file test.c
 * @brief Day 28 – Performance optimizations in FreeRTOS tasks on ESP32 (ESP-IDF):
 *        replacing a queue with direct-to-task notifications, and adding a
 *        logging task that writes to UART protected by a mutex.
 *
 * This example extends the notification-based producer/consumer with a
 * dedicated logging task. All UART writes are serialized by a FreeRTOS mutex
 * to avoid interleaved output when multiple tasks log concurrently.
 *
 * Summary of primitives used:
 *  - Producer→Consumer path: direct-to-task notifications (value transfer).
 *  - System logging path: queue of fixed-size messages → logging task → UART.
 *  - UART critical section: FreeRTOS mutex (SemaphoreHandle_t).
 *
 * Build/Config prerequisites:
 *  - Enable runtime stats: CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y
 *    and CONFIG_FREERTOS_USE_TRACE_FACILITY=y.
 *  - This demo uses UART0 (console). Pins are left as default (-1) so output
 *    appears on the USB-CDC/serial console. If you prefer a dedicated UART,
 *    change UART_PORT and set pins appropriately.
 *
 * How to compare CPU usage:
 *  1) Run the earlier queue-based data path and record vTaskGetRunTimeStats().
 *  2) Run the notification-based version (this file, before enabling logging).
 *  3) Enable the logging task (default here) and observe added overhead.
 *     Expect slightly more CPU use in the Logger task and slightly less
 *     interleaving/corruption on UART output under concurrent logging.
 */
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/uart.h"

#define TAG                 "DAY28"
#define UART_PORT           UART_NUM_0
#define LOG_MSG_MAX         64
#define LOG_QUEUE_LEN       16

/* Forward declaration to allow the producer to notify the consumer. */
static TaskHandle_t s_consumer_handle = NULL;

/* UART mutex and logging queue (messages are fixed-size C strings). */
static SemaphoreHandle_t s_uart_mutex = NULL;
static QueueHandle_t s_log_queue = NULL;

/**
 * uart_init_safe
 *
 * Configure the UART for simple TX logging and create a mutex to protect
 * uart_write_bytes() from concurrent access.
 *
 * Returns:
 *   void
 */
static void uart_init_safe(void) {
    /* Basic UART configuration: 115200-8-N-1 on UART0 (console). */
    const uart_config_t cfg = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
#if SOC_UART_SUPPORT_REF_TICK
        .source_clk = UART_SCLK_REF_TICK,
#else
        .source_clk = UART_SCLK_APB,
#endif
    };
    (void)uart_driver_install(UART_PORT, 1024, 0, 0, NULL, 0);
    (void)uart_param_config(UART_PORT, &cfg);
    /* Use default console pins. For a dedicated port, set explicit pins here. */
    (void)uart_set_pin(UART_PORT, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    /* Create the mutex if not already created. */
    if (!s_uart_mutex) {
        s_uart_mutex = xSemaphoreCreateMutex();
    }
}

/**
 * uart_log_line
 *
 * Thread-safe helper to write a C string plus a newline to the UART. Uses a
 * mutex to serialize access among tasks to prevent interleaved bytes.
 *
 * Args:
 *   line: NUL-terminated string to send. Must not be NULL.
 *
 * Notes:
 *   - If the mutex cannot be taken within a short time, the function drops
 *     the message to avoid blocking time-critical tasks.
 */
static void uart_log_line(const char *line) {
    if (!line || !s_uart_mutex) return;
    if (xSemaphoreTake(s_uart_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        size_t len = strnlen(line, LOG_MSG_MAX);
        (void)uart_write_bytes(UART_PORT, line, len);
        (void)uart_write_bytes(UART_PORT, "", 2);
        (void)xSemaphoreGive(s_uart_mutex);
    }
}

/**
 * logging_task
 *
 * Dedicated logger that dequeues short text messages and writes them to UART
 * under a mutex to guarantee atomicity per line.
 *
 * Args:
 *   pvParameters: (void*) Unused; must be NULL.
 *
 * Queue contract:
 *   - Each element is a char[LOG_MSG_MAX] buffer containing a NUL-terminated
 *     line (without trailing CR/LF). The task appends CRLF.
 */
static void logging_task(void *pvParameters) {
    (void)pvParameters;
    char msg[LOG_MSG_MAX];
    for (;;) {
        if (xQueueReceive(s_log_queue, &msg, portMAX_DELAY) == pdTRUE) {
            uart_log_line(msg);
        }
    }
}

/**
 * producer_task
 *
 * Generates a monotonically increasing integer at 10 Hz and notifies the
 * consumer task with the latest value using xTaskNotify() and the action
 * eSetValueWithOverwrite (so the most recent value wins if the consumer is
 * briefly busy). Also posts a short message to the logging queue (non-blocking).
 *
 * Args:
 *   pvParameters: (void*) Unused; must be NULL.
 */
static void producer_task(void *pvParameters) {
    (void)pvParameters;
    uint32_t count = 0;
    char buf[LOG_MSG_MAX];
    while (1) {
        count++;
        if (s_consumer_handle) {
            (void)xTaskNotify(s_consumer_handle, count, eSetValueWithOverwrite);
        }
        /* Best-effort enqueue of a log line (do not block). */
        int n = snprintf(buf, sizeof(buf), "Producer sent: %" PRIu32, count);
        if (n > 0) {
            (void)xQueueSend(s_log_queue, &buf, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(100)); /* 10 Hz */
    }
}

/**
 * consumer_task
 *
 * Blocks on xTaskNotifyWait() to receive the latest 32-bit value sent by the
 * producer via a direct-to-task notification. After processing, it enqueues a
 * short message to the logging queue for the logger to print.
 *
 * Args:
 *   pvParameters: (void*) Unused; must be NULL.
 */
static void consumer_task(void *pvParameters) {
    (void)pvParameters;
    uint32_t received = 0;
    char buf[LOG_MSG_MAX];
    for (;;) {
        (void)xTaskNotifyWait(/* entry clr */ 0,
                              /* exit  clr */ ULONG_MAX,
                              /* value     */ &received,
                              /* wait      */ portMAX_DELAY);
        /* Do lightweight processing... then log via queue (non-blocking). */
        int n = snprintf(buf, sizeof(buf), "Consumer processed: %" PRIu32, received);
        if (n > 0) {
            (void)xQueueSend(s_log_queue, &buf, 0);
        }
    }
}

/**
 * stats_task
 *
 * Periodically prints runtime statistics for all tasks using
 * vTaskGetRunTimeStats(). This helps validate that optimizations (priorities,
 * notification mechanism, logger overhead) behave as expected.
 *
 * Args:
 *   pvParameters: (void*) Unused; must be NULL.
 */
static void stats_task(void *pvParameters) {
    (void)pvParameters;
    char buffer[256];
    while (1) {
        vTaskGetRunTimeStats(buffer);
        ESP_LOGI(TAG, "Runtime Stats (notify + logger): %s", buffer);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

/**
 * app_main
 *
 * Application entry point. Initializes a mutex-protected UART, creates the log
 * queue, and spawns the producer, consumer, logger, and statistics tasks with
 * tuned stack sizes and priorities.
 *
 * Design:
 *   - Priorities: Consumer(5) > Producer(4) > Logger(3) ≈ Stats(3) to prioritize
 *     latency on the data path while keeping logging responsive.
 *   - Logger path: queue of short strings → logging_task → uart_log_line().
 *
 * Returns:
 *   void
 */
void app_main(void) {
    /* Init UART + mutex and logging queue. */
    uart_init_safe();
    s_log_queue = xQueueCreate(LOG_QUEUE_LEN, sizeof(char[LOG_MSG_MAX]));

    /* Create tasks with tuned priorities and modest stack sizes. */
    xTaskCreate(consumer_task, "Consumer", 2048, NULL, 5, &s_consumer_handle);
    xTaskCreate(producer_task, "Producer", 2048, NULL, 4, NULL);
    xTaskCreate(logging_task,  "Logger",   3072, NULL, 3, NULL);
    xTaskCreate(stats_task,    "Stats",    4096, NULL, 3, NULL);

    ESP_LOGI(TAG, "Day 28: Task notifications + mutex-protected UART logger started");

    /* Create tasks with tuned priorities and modest stack sizes. */
    xTaskCreate(consumer_task, "Consumer", 2048, NULL, 5, &s_consumer_handle);
    xTaskCreate(producer_task, "Producer", 2048, NULL, 4, NULL);
    xTaskCreate(stats_task,    "Stats",    4096, NULL, 3, NULL);

    ESP_LOGI(TAG, "Day 28: Using task notifications instead of a queue");
}
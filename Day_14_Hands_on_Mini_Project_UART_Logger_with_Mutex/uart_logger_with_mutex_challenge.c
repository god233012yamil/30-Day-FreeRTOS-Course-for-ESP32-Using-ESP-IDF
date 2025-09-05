/**
 * @file main.c
 * @brief Thread-safe UART logger variants on ESP32 using FreeRTOS: direct (unsafe), direct+mutex, and queue-based.
 *
 * @details
 * ## What this demo shows
 * 1) Three producer tasks concurrently "log" to the UART:
 *    - Task A: every 500 ms
 *    - Task B: every 800 ms
 *    - Task T: every 2000 ms, prints a timestamp (tick count)
 *
 * 2) Three logging architectures (select via LOG_MODE):
 *    - LOG_MODE_DIRECT       : Direct UART writes, NO mutex (expect line interleaving/corruption).
 *    - LOG_MODE_DIRECT_MUTEX : Direct UART writes protected by a FreeRTOS mutex (clean lines).
 *    - LOG_MODE_QUEUE        : Producers send messages to a dedicated logger task via FreeRTOS queue.
 *                              Only the logger task touches the UART (clean lines; ISR-friendly pattern).
 *
 * ## How to use
 * - Set one of the modes below (LOG_MODE_*).
 * - Build & run with ESP-IDF:
 *      idf.py set-target esp32
 *      idf.py build flash monitor
 *
 * ## Notes
 * - The queue-based design is the recommended foundation for production because ISRs and
 *   time-critical tasks can enqueue quickly without blocking on UART I/O.
 * - For ISR origins, use `xQueueSendFromISR()` and keep ISR code minimal.
 * - The mutex provides priority inheritance; a binary semaphore would not — useful to study.
 *
 * This file extends the user-provided code with additional tasks, modes, and documentation.
 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "driver/uart.h"

// ----------------------------- Configuration -----------------------------

#define TAG "DAY14"
#define UART_PORT UART_NUM_0
#define BUF_SIZE 1024

/**
 * @brief Logging modes.
 *
 * - LOG_MODE_DIRECT:        Direct UART writes without any protection (expect corruption).
 * - LOG_MODE_DIRECT_MUTEX:  Direct UART writes protected by a mutex.
 * - LOG_MODE_QUEUE:         Queue-based logger task; only the logger task touches UART.
 */
#define LOG_MODE_DIRECT        0
#define LOG_MODE_DIRECT_MUTEX  1
#define LOG_MODE_QUEUE         2

/** @brief Select the logging mode here. */
#ifndef LOG_MODE
#define LOG_MODE LOG_MODE_DIRECT_MUTEX
#endif

/** @brief Maximum length of one log line including terminating NUL. */
#define LOG_LINE_MAX 160

// --------------------------- Global Resources ----------------------------

/**
 * @brief Mutex guarding access to the UART (used in LOG_MODE_DIRECT_MUTEX).
 *
 * @details
 * Created in `app_main()` when LOG_MODE_DIRECT_MUTEX is active. Writers must
 * take/give this mutex to keep each line atomic and non-interleaved.
 */
static SemaphoreHandle_t g_uart_mutex = NULL;

/**
 * @brief Queue handle for queue-based logger (used in LOG_MODE_QUEUE).
 *
 * @details
 * Producers send fixed-size messages (`LogMsg`) to this queue. The logger task
 * reads from the queue and writes to UART, centralizing I/O and avoiding
 * contention on UART from multiple tasks.
 */
static QueueHandle_t g_log_queue = NULL;

// ------------------------------ Data Types --------------------------------

/**
 * @brief Fixed-size log message envelope used by the queue-based logger.
 */
typedef struct {
    char line[LOG_LINE_MAX];  ///< NUL-terminated log line (single line).
} LogMsg;

// ------------------------------ Functions --------------------------------

/**
 * @brief Low-level UART write of a preformatted C string + newline.
 *
 * @param msg Null-terminated C string. A newline is appended by this function.
 *
 * @details
 * This is the final sink to the UART hardware. It does not perform any synchronization.
 * Callers must ensure thread-safety (e.g., by holding a mutex or by being the single
 * consumer in the queue-based logger task).
 */
static inline void uart_write_line_raw(const char *msg) {
    if (!msg) return;
    uart_write_bytes(UART_PORT, msg, (size_t)strlen(msg));
    uart_write_bytes(UART_PORT, "\n", 1);
}

/**
 * @brief Direct logging helper: writes to UART with or without a mutex depending on mode.
 *
 * @param msg Null-terminated string to print as a line.
 *
 * @details
 * - In LOG_MODE_DIRECT:        Prints without any locking (unsafe under concurrency).
 * - In LOG_MODE_DIRECT_MUTEX:  Takes the mutex around the write to ensure atomicity.
 * - In LOG_MODE_QUEUE:         This function is not used; see `log_enqueue_line()` instead.
 */
static void direct_logger(const char *msg) {
#if LOG_MODE == LOG_MODE_DIRECT
    uart_write_line_raw(msg);
#elif LOG_MODE == LOG_MODE_DIRECT_MUTEX
    if (g_uart_mutex && xSemaphoreTake(g_uart_mutex, portMAX_DELAY) == pdTRUE) {
        uart_write_line_raw(msg);
        xSemaphoreGive(g_uart_mutex);
    }
#else
    (void)msg; // Unused in queue mode
#endif
}

/**
 * @brief Queue-based logging API: enqueue a formatted line for the logger task.
 *
 * @param fmt printf-style format string.
 * @param ... Variadic arguments for the format string.
 *
 * @details
 * Formats into a fixed-size buffer and sends to `g_log_queue`. The logger task
 * is the sole UART writer, providing serialized output and simplifying ISR usage
 * (use the FromISR variant if called in an ISR).
 *
 * @note Only used in LOG_MODE_QUEUE.
 */
static void log_enqueue_line(const char *fmt, ...) {
#if LOG_MODE == LOG_MODE_QUEUE
    if (!g_log_queue) return;

    LogMsg msg = {0};
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg.line, sizeof(msg.line), fmt, ap);
    va_end(ap);

    // Best-effort send; wait if queue is full to preserve lines.
    xQueueSend(g_log_queue, &msg, portMAX_DELAY);
#else
    (void)fmt;
#endif
}

/**
 * @brief Queue-based logger task: drains `g_log_queue` and writes to UART.
 *
 * @param pvParameter Unused (pass NULL).
 *
 * @details
 * Only this task accesses the UART in LOG_MODE_QUEUE. This centralization ensures that
 * all lines are printed atomically without needing a mutex, and enables future ISR-safe
 * patterns with `xQueueSendFromISR()`.
 */
static void logger_task(void *pvParameter) {
    (void)pvParameter;
#if LOG_MODE == LOG_MODE_QUEUE
    LogMsg msg;
    while (1) {
        if (xQueueReceive(g_log_queue, &msg, portMAX_DELAY) == pdTRUE) {
            uart_write_line_raw(msg.line); // single writer, no mutex needed
        }
    }
#else
    // Should not be created in other modes
    vTaskDelete(NULL);
#endif
}

/**
 * @brief Producer Task A: logs a message every 500 ms.
 *
 * @param pvParameter Unused (pass NULL).
 *
 * @details
 * Demonstrates frequent logging. In DIRECT mode, expect interleaving with other tasks.
 * In DIRECT_MUTEX/QUEUE modes, lines remain intact.
 */
static void taskA(void *pvParameter) {
    (void)pvParameter;
    while (1) {
#if LOG_MODE == LOG_MODE_QUEUE
        log_enqueue_line("Task A: Writing log message...");
#else
        direct_logger("Task A: Writing log message...");
#endif
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/**
 * @brief Producer Task B: logs a message every 800 ms.
 *
 * @param pvParameter Unused (pass NULL).
 *
 * @details
 * Logs at a different cadence to exacerbate contention and reveal line tearing
 * when running without synchronization (LOG_MODE_DIRECT).
 */
static void taskB(void *pvParameter) {
    (void)pvParameter;
    while (1) {
#if LOG_MODE == LOG_MODE_QUEUE
        log_enqueue_line("Task B: Writing log message...");
#else
        direct_logger("Task B: Writing log message...");
#endif
        vTaskDelay(pdMS_TO_TICKS(800));
    }
}

/**
 * @brief Producer Task T (timestamp): logs current tick count every 2000 ms.
 *
 * @param pvParameter Unused (pass NULL).
 *
 * @details
 * Prints the FreeRTOS tick count (converted to ms). It’s a third concurrent talker
 * that makes tearing easier to spot when there is no serialization in place.
 */
static void taskT(void *pvParameter) {
    (void)pvParameter;
    while (1) {
        TickType_t ticks = xTaskGetTickCount();
        uint32_t ms = (uint32_t) (ticks * (1000 / configTICK_RATE_HZ));
#if LOG_MODE == LOG_MODE_QUEUE
        log_enqueue_line("Task T: timestamp = %u ms (ticks=%lu)", (unsigned)ms, (unsigned long)ticks);
#else
        char buf[LOG_LINE_MAX];
        snprintf(buf, sizeof(buf), "Task T: timestamp = %u ms (ticks=%lu)", (unsigned)ms, (unsigned long)ticks);
        direct_logger(buf);
#endif
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

/**
 * @brief Initialize and install the UART driver with default settings.
 *
 * @details
 * 115200-8-N-1, no flow control. RX/TX buffers are both set to BUF_SIZE.
 */
static void uart_init(void) {
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
        // .source_clk = UART_SCLK_DEFAULT, // (uncomment on newer ESP-IDF if needed)
    };
    uart_driver_install(UART_PORT, BUF_SIZE, BUF_SIZE, 0, NULL, 0);
    uart_param_config(UART_PORT, &uart_config);
}

/**
 * @brief ESP-IDF entry point: sets up UART and launches the selected logging architecture.
 *
 * @details
 * - LOG_MODE_DIRECT:
 *     * No mutex, no queue. Expect interleaved/garbled lines during concurrency.
 * - LOG_MODE_DIRECT_MUTEX:
 *     * Creates a mutex and uses it to serialize UART writes.
 * - LOG_MODE_QUEUE:
 *     * Creates a queue and a logger task; producers enqueue lines for a single UART writer.
 */
void app_main(void) {
    uart_init();

#if LOG_MODE == LOG_MODE_DIRECT_MUTEX
    g_uart_mutex = xSemaphoreCreateMutex();
    if (!g_uart_mutex) {
        printf("Failed to create UART mutex\n");
        return;
    }
#elif LOG_MODE == LOG_MODE_QUEUE
    g_log_queue = xQueueCreate(16, sizeof(LogMsg));
    if (!g_log_queue) {
        printf("Failed to create log queue\n");
        return;
    }
    xTaskCreate(logger_task, "logger_task", 3072, NULL, 6, NULL);
#endif

    // Create producer tasks. Same priority -> round-robin on same core.
    xTaskCreate(taskA, "TaskA", 2048, NULL, 5, NULL);
    xTaskCreate(taskB, "TaskB", 2048, NULL, 5, NULL);
    xTaskCreate(taskT, "TaskT", 2048, NULL, 5, NULL);

#if LOG_MODE == LOG_MODE_DIRECT
    printf("\n[INFO] LOG_MODE_DIRECT: No mutex, expect interleaving/tearing.\n\n");
#elif LOG_MODE == LOG_MODE_DIRECT_MUTEX
    printf("\n[INFO] LOG_MODE_DIRECT_MUTEX: Mutex-protected UART writes.\n\n");
#elif LOG_MODE == LOG_MODE_QUEUE
    printf("\n[INFO] LOG_MODE_QUEUE: Queue-based centralized logger task.\n\n");
#endif
}
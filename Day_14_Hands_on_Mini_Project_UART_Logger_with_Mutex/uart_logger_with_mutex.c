/**
 * @file main.c
 * @brief Thread-safe UART logger on ESP32 using FreeRTOS mutex.
 *
 * @details
 * ## Purpose
 * Demonstrate how to serialize writes to a shared UART peripheral from
 * multiple FreeRTOS tasks using a mutex (priority-inheritance aware)
 * to guarantee log line integrity and avoid interleaved output.
 *
 * ## How It Works
 * - A global `SemaphoreHandle_t` (mutex) guards access to the UART.
 * - A helper function `uart_logger()` takes the mutex, writes a message
 *   and a newline atomically, then releases the mutex.
 * - Two tasks (`taskA`, `taskB`) periodically call `uart_logger()` at
 *   different rates to show clean, non-interleaved UART output.
 *
 * ## Thread Safety
 * All UART writes go through `uart_logger()`, which is protected
 * by a FreeRTOS mutex. This ensures one task at a time can write
 * to the UART and also provides priority inheritance to mitigate
 * priority inversion scenarios typical in logging paths.
 *
 * ## Configuration
 * - UART: `UART_NUM_0` at 115200-8-N-1, no flow control.
 * - Stack sizes and priorities can be tuned per application needs.
 *
 * ## Build & Run (ESP-IDF)
 * 1. Put this file in your project's `main/` folder.
 * 2. `idf.py set-target esp32` (or esp32s3/esp32c6 as appropriate).
 * 3. `idf.py build flash monitor`
 *
 * ## Notes
 * - If you later add more producers (tasks/ISRs), keep using this single
 *   logging API to maintain atomic output. For ISR contexts, use a
 *   different path (e.g., a queue to a logger task) since mutexes are not
 *   intended for ISR use.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/uart.h"

// ----------------------------- Configuration -----------------------------

#define TAG "DAY14"
#define UART_PORT UART_NUM_0
#define BUF_SIZE 1024

// --------------------------- Global Resources ----------------------------

// Mutex to guard UART access for thread-safe logging across multiple tasks.
static SemaphoreHandle_t uart_mutex = NULL;

// ------------------------------ Functions --------------------------------

/**
 * @brief Thread-safe UART print of a message followed by a newline.
 *
 * @param msg Null-terminated C-string to be written to the UART.
 *
 * @details
 * This function blocks until it can take the mutex, then writes the
 * message and a trailing newline as a single critical section. Using
 * a mutex prevents log interleaving across multiple tasks. If the
 * mutex is unavailable (e.g., not created), this call becomes a no-op.
 *
 * @note
 * - Designed for task context (not ISR). For ISR-originated logs,
 *   route text to a queue consumed by a logger task, or use a
 *   minimal ISR that signals a task to perform the write.
 * - Uses `uart_write_bytes()`, which is buffered by the UART driver.
 */
static void uart_logger(const char *msg) {
    if (!msg || !uart_mutex) {
        return;
    }
    if (xSemaphoreTake(uart_mutex, portMAX_DELAY) == pdTRUE) {
        // Write the message and a newline to the UART in a single critical section.
        uart_write_bytes(UART_PORT, msg, (size_t)strlen(msg));
        // Write a newline character to separate log entries. This ensures each log entry appears on its own line.
        uart_write_bytes(UART_PORT, "\n", 1);
        // Release the mutex after the write is complete.
        xSemaphoreGive(uart_mutex);
    }
}

/**
 * @brief Periodic logger task A.
 *
 * @param pvParameter Unused (pass NULL).
 *
 * @details
 * Prints a message every 500 ms via the thread-safe `uart_logger()`.
 * Demonstrates that even with frequent prints, lines remain intact.
 *
 * @pre `uart_mutex` must be created in `app_main()` before this task starts.
 */
static void taskA(void *pvParameter) {
    (void)pvParameter;
    while (1) {
        uart_logger("Task A: Writing log message...");
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/**
 * @brief Periodic logger task B.
 *
 * @param pvParameter Unused (pass NULL).
 *
 * @details
 * Prints a message every 800 ms via the thread-safe `uart_logger()`.
 * Running alongside Task A showcases serialized access to the UART.
 *
 * @pre `uart_mutex` must be created in `app_main()` before this task starts.
 */
static void taskB(void *pvParameter) {
    (void)pvParameter;
    while (1) {
        uart_logger("Task B: Writing log message...");
        vTaskDelay(pdMS_TO_TICKS(800));
    }
}

/**
 * @brief ESP-IDF entry point: initializes UART, creates mutex and tasks.
 *
 * @details
 * 1. Creates a mutex to guard UART writes.
 * 2. Installs and configures UART driver for `UART_PORT`.
 * 3. Spawns two logger tasks (`taskA` and `taskB`) with equal priority.
 *
 * @note
 * - If mutex creation fails, the function prints an error and returns,
 *   leaving no tasks running.
 * - Adjust `baud_rate`, task stack sizes, and priorities as required.
 */
void app_main(void) {
    // Create UART mutex.
    uart_mutex = xSemaphoreCreateMutex();
    if (uart_mutex == NULL) {
        printf("Failed to create mutex\n");
        return;
    }

    // Configure UART.
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
        // For newer ESP-IDF you may also set:
        // .source_clk = UART_SCLK_DEFAULT,
    };

    // Install UART driver and apply parameters.
    // RX/TX buffers are both allocated at BUF_SIZE; no event queue used.
    uart_driver_install(UART_PORT, BUF_SIZE, BUF_SIZE, 0, NULL, 0);
    uart_param_config(UART_PORT, &uart_config);

    // Create logging tasks (same priority -> time-sliced on equal priority cores).
    xTaskCreate(taskA, "TaskA", 2048, NULL, 5, NULL);
    xTaskCreate(taskB, "TaskB", 2048, NULL, 5, NULL);
}
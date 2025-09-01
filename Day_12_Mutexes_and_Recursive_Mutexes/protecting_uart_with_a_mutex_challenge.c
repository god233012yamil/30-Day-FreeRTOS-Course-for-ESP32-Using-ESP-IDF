// main.c
#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"  // for esp_timer_get_time()

/*
 * Build-time switches:
 *   USE_MUTEX = 1  -> use xSemaphoreCreateMutex() with priority inheritance
 *   USE_MUTEX = 0  -> use xSemaphoreCreateBinary() (no priority inheritance)
 *   ENABLE_MEDIUM_DISTURBER = 1 -> adds a medium-priority CPU task that
 *                                  aggravates inversion when USE_MUTEX = 0
 */
#define USE_MUTEX                 1
#if (USE_MUTEX == 0)
#define ENABLE_MEDIUM_DISTURBER   1
#else
#define ENABLE_MEDIUM_DISTURBER   0
#endif

#define TAG "DAY12+"
#define UART_LOCK_TAKE_TIMEOUT    portMAX_DELAY

// Task priorities chosen to demonstrate priority relationships:
// - Low writer tends to hold the UART lock for longer
// - Medium disturber (optional) preempts the low task when lock is held
// - High timestamp logger is blocked waiting for the lock
#define PRIO_LOW_WRITER     1
#define PRIO_MEDIUM_TASK    3
#define PRIO_HIGH_LOGGER    4

// Shared locking primitive (mutex or binary semaphore)
static SemaphoreHandle_t uart_lock = NULL;

// Simulated UART write (guarded by the lock)
static void uart_write_line(const char *owner, const char *line, int idx)
{
    // In a real design, this would call uart_write_bytes(), etc.
    // Here we simulate a multi-line "transaction" that we want to keep intact.
    printf("[%s] %s %d\n", owner, line, idx);
}

// Task A/B: writers that send 5 lines per "transaction" 
static void uart_writer_task(void *pv)
{
    const char *name = (const char *)pv;

    while (1) {
        // Take the lock before starting a multi-line write sequence
        if (xSemaphoreTake(uart_lock, UART_LOCK_TAKE_TIMEOUT) == pdTRUE) {

            // Simulate a relatively long critical section while holding the UART
            for (int i = 0; i < 5; ++i) {
                uart_write_line(name, "writing line", i);
                vTaskDelay(pdMS_TO_TICKS(200)); // make the hold noticeable
            }

            // Release the lock when "transaction" is complete
            xSemaphoreGive(uart_lock);
        }

        // Wait before next transaction
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Task C: high-priority timestamp logger 
static void timestamp_logger_task(void *pv)
{
    (void)pv;
    while (1) {
        // Log every second, but serialize access via the same lock
        if (xSemaphoreTake(uart_lock, UART_LOCK_TAKE_TIMEOUT) == pdTRUE) {

            // esp_timer_get_time() returns microseconds since boot
            int64_t us = esp_timer_get_time();
            TickType_t ticks = xTaskGetTickCount();

            // Print 3 lines as a "transaction"
            for (int i = 0; i < 3; ++i) {
                printf("[TimeLogger] t_us=%" PRId64 ", ticks=%" PRIu32 ", entry=%d\n",
                       us, (uint32_t)ticks, i);
                vTaskDelay(pdMS_TO_TICKS(80));
            }

            xSemaphoreGive(uart_lock);
        }

        vTaskDelay(pdMS_TO_TICKS(1000)); // log roughly once per second
    }
}

// Optional medium-priority "disturber" task (does NOT use the UART)
#if (ENABLE_MEDIUM_DISTURBER == 1)
static void medium_disturber_task(void *pv)
{
    (void)pv;
    // This task burns some CPU time periodically without ever taking the lock.
    // With a binary semaphore (no PI), it can repeatedly preempt the low-priority
    // writer that holds the UART, thereby delaying the high-priority logger that
    // is blocked waiting for the semaphore (priority inversion).
    const TickType_t work_ms = 150;   // "busy work" time
    const TickType_t idle_ms = 25;    // brief pause

    while (1) {
        // Simulate work by delaying in small quanta (lets scheduler run)
        TickType_t t_end = xTaskGetTickCount() + pdMS_TO_TICKS(work_ms);
        while (xTaskGetTickCount() < t_end) {
            // do nothing; just yield frequently
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        vTaskDelay(pdMS_TO_TICKS(idle_ms));
    }
}
#endif

// Main application entry point
void app_main(void)
{
#if (USE_MUTEX == 1)
    // Create a mutex, which implements priority inheritance
    uart_lock = xSemaphoreCreateMutex();
    if (!uart_lock) {
        printf("Failed to create mutex\n");
        return;
    }
#else
    // Create a binary semaphore, which does NOT implement priority inheritance
    uart_lock = xSemaphoreCreateBinary();
    if (!uart_lock) {
        printf("Failed to create binary semaphore\n");
        return;
    }
    // Binary semaphore is created "empty" — give it once so the first taker succeeds.
    xSemaphoreGive(uart_lock);
#endif

    // Create one low-priority writer that tends to hold the lock a bit longer
    xTaskCreate(uart_writer_task, "WriterLow",  2048, "Writer L", PRIO_LOW_WRITER,  NULL);

    // Create a second writer at medium priority that also uses the UART
    xTaskCreate(uart_writer_task, "WriterMed",  2048, "Writer M", PRIO_MEDIUM_TASK, NULL);

    // Create the high-priority timestamp logger that also needs the UART
    xTaskCreate(timestamp_logger_task, "TimeLog", 2048, NULL,      PRIO_HIGH_LOGGER, NULL);

#if (ENABLE_MEDIUM_DISTURBER == 1)
    // Add a separate medium-priority task that never takes the lock
    // to aggravate priority inversion when USE_MUTEX == 0.
    xTaskCreate(medium_disturber_task, "Disturber", 2048, NULL, PRIO_MEDIUM_TASK, NULL);
#endif
}
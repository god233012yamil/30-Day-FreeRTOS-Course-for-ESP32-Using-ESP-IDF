/**
 * @file priority_inversion_toggle.c
 * @brief Toggle between Binary Semaphore (shows priority inversion) and Mutex (fixes it).
 *
 * How to use:
 *   - Set USE_BINARY_SEMAPHORE = 1 -> expect priority inversion (High waits long).
 *   - Set USE_BINARY_SEMAPHORE = 0 -> use mutex; Priority Inheritance reduces High's wait.
 *
 * What to observe:
 *   - With binary: "High waited ~XXXX ms for UART" tends to be long while "Medium" logs keep printing.
 *   - With mutex : High’s measured wait shrinks dramatically, because Low inherits High’s priority
 *                  while it holds the mutex, finishes, and releases it sooner.
 *
 * Notes:
 *   - All tasks are pinned to the same core for a reproducible schedule.
 *   - Medium simulates meaningful CPU load to make the inversion clearly visible.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_log.h"

#define TAG "PI_TOGGLE"

// ========== Toggle here ==========
#define USE_BINARY_SEMAPHORE   1   // 1 = show inversion; 0 = fix with mutex
#define PIN_TO_CORE            0   // choose 0 or 1 on dual-core; keep 0 on single-core
// =================================

// Priorities (higher number = higher priority)
#define LOW_PRIO     2
#define MED_PRIO     5
#define HIGH_PRIO   10

// Timings (ms)
#define LOW_HOLD_MS        2000   // Low holds the "UART" this long (inside critical section)
#define HIGH_USE_MS         500   // High uses the UART briefly
#define MED_BUSY_SLICE_MS    20   // Medium burns CPU for this long each loop (busy)
#define MED_YIELD_MS          2   // Small delay to keep WDT happy & logs readable
#define HIGH_START_DELAY_MS  500  // Stagger High so Low often holds the resource first

static SemaphoreHandle_t s_resource;  // UART guard: binary semaphore OR mutex

// Busy spin for ~ms (to actually preempt Low when High is blocked)
static void busy_ms(uint32_t ms)
{
    int64_t t0 = esp_timer_get_time();
    int64_t until = t0 + (int64_t)ms * 1000;
    while (esp_timer_get_time() < until) {
        __asm__ __volatile__("" ::: "memory"); // prevent over-optimization
    }
}

// Low: takes the resource, holds it for a while (simulating long critical section)
static void low_task(void *pv)
{
    (void)pv;
    while (1) {
        if (xSemaphoreTake(s_resource, portMAX_DELAY)) {
            ESP_LOGI(TAG, "[LOW ] acquired UART, holding for %d ms", LOW_HOLD_MS);
            vTaskDelay(pdMS_TO_TICKS(LOW_HOLD_MS));
            ESP_LOGI(TAG, "[LOW ] releasing UART");
            xSemaphoreGive(s_resource);
        }
        vTaskDelay(pdMS_TO_TICKS(1000)); // some non-critical idle time
    }
}

// High: needs the same resource; we measure how long it waits
static void high_task(void *pv)
{
    (void)pv;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(HIGH_START_DELAY_MS)); // start a bit later
        // Try to take the resource, measuring how long we wait
        int64_t t0 = esp_timer_get_time();
        xSemaphoreTake(s_resource, portMAX_DELAY);
        int64_t waited_us = esp_timer_get_time() - t0;

        ESP_LOGW(TAG, "[HIGH] acquired UART after waiting ~%lld ms", (long long)(waited_us / 1000));
        vTaskDelay(pdMS_TO_TICKS(HIGH_USE_MS));
        ESP_LOGW(TAG, "[HIGH] releasing UART");
        xSemaphoreGive(s_resource);

        vTaskDelay(pdMS_TO_TICKS(300));
    }
}

// Medium: CPU load to make inversion obvious (preempts Low when High is blocked)
static void medium_task(void *pv)
{
    (void)pv;
    while (1) {
        ESP_LOGI(TAG, "[MED ] running (CPU load)");
        busy_ms(MED_BUSY_SLICE_MS);       // actively burn CPU at priority 5
        vTaskDelay(pdMS_TO_TICKS(MED_YIELD_MS)); // brief pause for WDT/log readability
    }
}

void app_main(void)
{
#if USE_BINARY_SEMAPHORE
    ESP_LOGI(TAG, "Mode: BINARY SEMAPHORE (expect PRIORITY INVERSION)");
    // Create a binary semaphore (no priority inheritance)
    s_resource = xSemaphoreCreateBinary();
    configASSERT(s_resource != NULL);
    // Binary semaphores are created "empty"; give once so Low can take it:
    xSemaphoreGive(s_resource);
#else
    ESP_LOGI(TAG, "Mode: MUTEX (Priority Inheritance active; inversion mitigated)");
    s_resource = xSemaphoreCreateMutex(); // enables PI in FreeRTOS
    configASSERT(s_resource != NULL);
#endif

    // Create tasks pinned to the same core to avoid cross-core scheduling noise.
    xTaskCreatePinnedToCore(low_task,    "LowTask",  3072, NULL, LOW_PRIO,  NULL, PIN_TO_CORE);
    xTaskCreatePinnedToCore(medium_task, "MedTask",  3072, NULL, MED_PRIO,  NULL, PIN_TO_CORE);
    xTaskCreatePinnedToCore(high_task,   "HighTask", 3072, NULL, HIGH_PRIO, NULL, PIN_TO_CORE);

    ESP_LOGI(TAG, "Started. Flip USE_BINARY_SEMAPHORE to compare behavior.");
}
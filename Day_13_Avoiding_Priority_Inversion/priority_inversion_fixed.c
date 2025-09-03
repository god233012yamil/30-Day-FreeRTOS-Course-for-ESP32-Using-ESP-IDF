/**
 * @file priority_inversion_fixed.c
 * @brief ESP-IDF demo: mitigating priority inversion with (a) proper mutex use
 *        and (b) optional priority-ceiling boost around the shared resource.
 *
 * What it shows:
 *  - Using a FreeRTOS mutex (with built-in Priority Inheritance) to serialize access
 *    to a shared "UART" resource.
 *  - Keeping the critical section short (do work outside the lock).
 *  - (Optional) Applying a priority-ceiling guard: temporarily boost the owner's
 *    priority while it holds the resource, then restore it on release.
 *
 * Notes:
 *  - Pin all tasks to the same core for a stable reproduction.
 *  - The "medium" task simulates heavy CPU activity that would otherwise starve
 *    the low task and worsen inversion.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_log.h"

#define TAG "PI_FIX"

// ---- Configuration ----------------------------------------------------------

// Choose a core to pin tasks (0 or 1 on dual-core parts)
#ifndef PI_DEMO_CORE
#define PI_DEMO_CORE 0
#endif

// Enable priority-ceiling boost while holding the resource
#ifndef USE_PRIORITY_CEILING
#define USE_PRIORITY_CEILING 1
#endif

// Task priorities (higher number = higher priority)
#define LOW_TASK_PRIO     2
#define MED_TASK_PRIO     5
#define HIGH_TASK_PRIO   10
// Priority ceiling to apply while holding the resource (>= HIGH_TASK_PRIO)
#define CEILING_PRIO     (HIGH_TASK_PRIO + 1)

// Simulated work durations (ms)
#define LOW_OUTSIDE_WORK_MS   1400   // work done OUTSIDE the lock
#define LOW_INSIDE_LOCK_MS     150   // tiny critical section (UART use)
#define HIGH_WORK_MS            80
#define MED_SLICE_MS            5    // CPU-busy slice
#define MED_YIELD_MS            1    // brief yield to make schedule observable

// ----------------------------------------------------------------------------

/** Mutex protecting the "UART" resource. Use a standard FreeRTOS mutex
 *  (NOT a binary semaphore) to enable priority inheritance. */
static SemaphoreHandle_t s_uart_mutex;

/** When we apply priority ceiling, store the owner's original priority here. */
static UBaseType_t s_low_task_orig_prio = LOW_TASK_PRIO;

/** Small helper to simulate busy CPU work for a given number of milliseconds. */
static void busy_ms(uint32_t ms)
{
    int64_t t0 = esp_timer_get_time();
    int64_t target = t0 + (int64_t)ms * 1000;
    while (esp_timer_get_time() < target) {
        // spin to consume CPU; prevent compiler from optimizing too much
        __asm__ __volatile__("" ::: "memory");
    }
}

/** Acquire the UART resource with optional priority ceiling. */
static void uart_lock_with_ceiling(TaskHandle_t owner)
{
#if USE_PRIORITY_CEILING
    // Boost owner's priority before we try to take the mutex, so the owner
    // won't be preempted by medium tasks while entering the critical section.
    s_low_task_orig_prio = uxTaskPriorityGet(owner);
    if (s_low_task_orig_prio < CEILING_PRIO) {
        vTaskPrioritySet(owner, CEILING_PRIO);
    }
#endif
    xSemaphoreTake(s_uart_mutex, portMAX_DELAY);
}

/** Release the UART resource and restore original priority if needed. */
static void uart_unlock_restore(TaskHandle_t owner)
{
    xSemaphoreGive(s_uart_mutex);
#if USE_PRIORITY_CEILING
    // Restore original priority after releasing the resource.
    if (uxTaskPriorityGet(owner) != s_low_task_orig_prio) {
        vTaskPrioritySet(owner, s_low_task_orig_prio);
    }
#endif
}

// ----------------------------- Tasks ----------------------------------------

/**
 * Low-priority task:
 *  - Does most of its work OUTSIDE the lock (simulate prep/formatting).
 *  - Briefly locks the UART to "print" a message.
 *  - Optional priority ceiling eliminates the window where medium could starve it.
 */
static void low_task(void *pv)
{
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    while (1) {
        // Long work OUTSIDE the critical section (cannot block others):
        ESP_LOGI(TAG, "Low: preparing data (outside lock)...");
        vTaskDelay(pdMS_TO_TICKS(LOW_OUTSIDE_WORK_MS)); // simulate prep

        // Now briefly use the UART (critical section kept small):
        uart_lock_with_ceiling(self);
        ESP_LOGI(TAG, "Low: acquired UART (inside lock)...");
        vTaskDelay(pdMS_TO_TICKS(LOW_INSIDE_LOCK_MS)); // short, bounded
        ESP_LOGI(TAG, "Low: releasing UART");
        uart_unlock_restore(self);

        // Small delay to make the pattern readable
        vTaskDelay(pdMS_TO_TICKS(120));
    }
}

/**
 * High-priority task:
 *  - Periodically needs the UART for short bursts.
 *  - When Low holds the mutex, PI ensures Low inherits High's priority and
 *    completes the critical section promptly.
 */
static void high_task(void *pv)
{
    while (1) {
        // Stagger start so Low likely holds the resource first at times
        vTaskDelay(pdMS_TO_TICKS(500));

        if (xSemaphoreTake(s_uart_mutex, portMAX_DELAY)) {
            ESP_LOGW(TAG, "HIGH: using UART");
            vTaskDelay(pdMS_TO_TICKS(HIGH_WORK_MS));
            xSemaphoreGive(s_uart_mutex);
            ESP_LOGW(TAG, "HIGH: released UART");
        }

        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

/**
 * Medium-priority task:
 *  - Simulates CPU load that would otherwise starve Low when High is blocked.
 *  - Uses a busy spin for a short slice to put real pressure on the scheduler,
 *    then yields briefly so logs remain readable.
 */
static void medium_task(void *pv)
{
    while (1) {
        // CPU-heavy work that would preempt Low (if Low weren't boosted/inherited):
        busy_ms(MED_SLICE_MS);
        // Brief yield to avoid total log flood:
        vTaskDelay(pdMS_TO_TICKS(MED_YIELD_MS));
    }
}

// ----------------------------- Entry ----------------------------------------

void app_main(void)
{
    // Create a mutex (priority inheritance enabled by design in FreeRTOS)
    s_uart_mutex = xSemaphoreCreateMutex();
    configASSERT(s_uart_mutex != NULL);

    // Create tasks pinned to one core to make scheduling behavior predictable.
    xTaskCreatePinnedToCore(low_task,   "LowTask",   3072, NULL, LOW_TASK_PRIO,  NULL, PI_DEMO_CORE);
    xTaskCreatePinnedToCore(medium_task,"MedTask",   3072, NULL, MED_TASK_PRIO,  NULL, PI_DEMO_CORE);
    xTaskCreatePinnedToCore(high_task,  "HighTask",  3072, NULL, HIGH_TASK_PRIO, NULL, PI_DEMO_CORE);

    ESP_LOGI(TAG, "Demo started. USE_PRIORITY_CEILING=%d, core=%d", USE_PRIORITY_CEILING, PI_DEMO_CORE);
}
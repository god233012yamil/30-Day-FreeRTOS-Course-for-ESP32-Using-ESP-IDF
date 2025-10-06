/**
 * @file        main.c
 * @author      Yamil Garcia
 *
 * @brief       ESP32 Task Watchdog (TWDT) demo with healthy, stuck, and flaky tasks,
 *              plus a deliberate stack overflow using the FreeRTOS stack overflow hook.
 *
 * This example configures the Task Watchdog Timer (WDT) and launches four tasks:
 *  - Healthy task: feeds the TWDT once per second (well-behaved).
 *  - Stuck task: never feeds the TWDT (simulates hard deadlock).
 *  - Flaky task: intermittently feeds the TWDT, then intentionally skips feeding long
 *    enough to trip the watchdog (simulates intermittent bugs).
 *  - Tiny-stack task: created with a very small stack and a function that consumes
 *    stack aggressively, causing a deliberate stack overflow. The overflow is caught
 *    by FreeRTOS via vApplicationStackOverflowHook().
 *
 * Notes:
 *  - Ensure stack overflow checking is enabled in menuconfig:
 *    Component config → FreeRTOS → Check for stack overflow → Method B (2).
 *  - TWDT can be auto-initialized by menuconfig; this code tolerates both cases.
 *  - TWDT timeout is 5 seconds in this demo.
 *
 * @version     0.3
 * @date        2025-10-02
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
#include "esp_log.h"
#include "esp_system.h"

#define TAG "DAY27_WDT"

// -----------------------------------------------------------------------------
// Forward declarations
// -----------------------------------------------------------------------------
static void healthy_task(void *pvParameter);
static void stuck_task(void *pvParameter);
static void flaky_task(void *pvParameter);
static void tiny_stack_task(void *pvParameter);
static void chew_stack_and_work(size_t bytes_to_burn, int iters);

// -----------------------------------------------------------------------------
// Tasks
// -----------------------------------------------------------------------------

/**
 * @brief Healthy task that simulates normal operation and regularly feeds TWDT.
 *
 * The task is added to the Task WDT watch list and resets (feeds) it every 1000 ms.
 * This represents a well-behaved, responsive task.
 *
 * @param pvParameter Unused task parameter (pass NULL).
 */
static void healthy_task(void *pvParameter)
{
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));  // watch this task
    while (1) {
        ESP_LOGI(TAG, "[Healthy] feeding TWDT");
        ESP_ERROR_CHECK(esp_task_wdt_reset());
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/**
 * @brief Stuck task that simulates a deadlock (never feeds the TWDT).
 *
 * The task is added to the Task WDT watch list but intentionally never calls
 * esp_task_wdt_reset(). After the TWDT timeout elapses, the system will panic/reset
 * (depending on configuration).
 *
 * @param pvParameter Unused task parameter (pass NULL).
 */
static void stuck_task(void *pvParameter)
{
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));  // watch this task
    ESP_LOGW(TAG, "[Stuck] will block forever without feeding TWDT...");
    while (1) {
        // Busy wait to simulate a hard lock (no feeds, no delays)
    }
}

/**
 * @brief Flaky task that intermittently feeds the TWDT and then skips feeding to trigger it.
 *
 * Behavior pattern (repeats):
 *  - Phase A: Feed the watchdog 3 times (every 1s).
 *  - Phase B: Intentionally skip feeding for 6s (greater than the 5s timeout).
 *
 * This simulates an intermittent bug where a task sometimes behaves and sometimes stalls.
 *
 * @param pvParameter Unused task parameter (pass NULL).
 */
static void flaky_task(void *pvParameter)
{
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));  // watch this task
    int cycle = 0;

    while (1) {
        // Phase A: behave for ~3 seconds
        for (int i = 0; i < 3; ++i) {
            ESP_LOGI(TAG, "[Flaky] cycle %d: feeding TWDT (%d/3)", cycle, i + 1);
            ESP_ERROR_CHECK(esp_task_wdt_reset());
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        // Phase B: misbehave for ~6 seconds (exceeds 5s TWDT timeout)
        ESP_LOGW(TAG, "[Flaky] cycle %d: simulating stall (>5s) without feeding...", cycle);
        vTaskDelay(pdMS_TO_TICKS(6000));

        // If we are still alive here, either TWDT didn't panic (e.g., trigger_panic=false)
        // or auto-initialization is disabled and this task isn't being watched as expected.
        ESP_LOGW(TAG, "[Flaky] cycle %d: still running after stall (check TWDT config).", cycle);

        ++cycle;
    }
}

/**
 * @brief Tiny-stack task designed to overflow its stack deliberately.
 *
 * The task is created with a small stack and repeatedly calls a helper that
 * allocates a large local array to burn stack space. With FreeRTOS stack overflow
 * checking enabled (Method B), an overflow will trigger vApplicationStackOverflowHook().
 *
 * @param pvParameter Unused task parameter (pass NULL).
 */
static void tiny_stack_task(void *pvParameter)
{
    // Not adding this task to TWDT; the goal is stack overflow, not watchdog.
    ESP_LOGI(TAG, "[TinyStack] starting with very small stack; will chew stack...");
    while (1) {
        // Burn ~2 KB per call; repeat enough times to exceed the small stack.
        chew_stack_and_work(2048, 4);
        // Short delay—if we somehow survive.
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/**
 * @brief Helper that allocates a large local buffer to consume stack space.
 *
 * This function is intentionally aggressive: it declares a VLA on the stack
 * and touches it to prevent optimization. Repeating the call quickly exceeds
 * a tiny task stack.
 *
 * @param bytes_to_burn Approximate number of bytes to allocate on the stack.
 * @param iters         Number of times to repeat the allocation loop.
 */
static void chew_stack_and_work(size_t bytes_to_burn, int iters)
{
    // Guard against pathological values
    if (bytes_to_burn == 0 || iters <= 0) {
        return;
    }

    // Allocate 'bytes_to_burn' on the stack (VLA) and touch it.
    // NOTE: VLAs are supported by GCC; this is for demo purposes only.
    for (int i = 0; i < iters; ++i) {
        volatile uint8_t sink = 0;
        uint8_t buf[bytes_to_burn];        // large on-purpose
        for (size_t k = 0; k < bytes_to_burn; k += 64) {
            buf[k] = (uint8_t)(k & 0xFF);
            sink ^= buf[k];
        }
        // Make sure compiler can't optimize away the loop completely.
        if (sink == 0xFF) {
            ESP_LOGD(TAG, "sink=0xFF");
        }
    }
}

// -----------------------------------------------------------------------------
// FreeRTOS / ESP-IDF hooks
// -----------------------------------------------------------------------------

/**
 * @brief FreeRTOS stack overflow hook (enabled when Method A/B is configured).
 *
 * This function is called by the kernel if it detects a stack overflow in any task.
 * We log the offending task and abort the system to make the fault visible.
 *
 * @param xTask      Handle of the task that overflowed its stack.
 * @param pcTaskName Name of the task that overflowed its stack.
 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    // Use early logging to increase odds of printing before abort/reset.
    ESP_EARLY_LOGE(TAG, "Stack overflow detected in task: %s", pcTaskName ? pcTaskName : "(unknown)");
    esp_system_abort("Stack overflow");
}

// -----------------------------------------------------------------------------
// app_main
// -----------------------------------------------------------------------------

/**
 * @brief Main application entry: configures TWDT and launches demo tasks.
 *
 * - Configures Task WDT to a 5s timeout, panic on timeout, and monitors idle tasks.
 * - Adds app_main itself to the TWDT watch list (optional but useful for demo).
 * - Creates four tasks:
 *     1) Healthy (feeds every 1s)
 *     2) Stuck (never feeds)
 *     3) Flaky (alternates feeding and stalling > timeout)
 *     4) TinyStack (very small stack to trigger stack overflow hook)
 */
void app_main(void)
{
    // Configure Task WDT (5s, panic on timeout, monitor idle tasks on all cores)
    static const esp_task_wdt_config_t twdt_cfg = {
        .timeout_ms = 5000,
        .trigger_panic = true,
        .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
    };

    // Initialize TWDT, tolerating auto-init at boot.
    esp_err_t err = esp_task_wdt_init(&twdt_cfg);
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "TWDT already initialized at boot; skipping init.");
    } else {
        ESP_ERROR_CHECK(err);
    }

    // Monitor app_main itself (optional)
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    // Create demo tasks.
    // NOTE: Stack sizes are in words (32-bit). 2048 words ≈ 8 KB on Xtensa.
    xTaskCreate(healthy_task,   "HealthyTask",   2048, NULL, 5, NULL);
    xTaskCreate(stuck_task,     "StuckTask",     2048, NULL, 5, NULL);
    xTaskCreate(flaky_task,     "FlakyTask",     2048, NULL, 5, NULL);

    // Create a tiny-stack task to force a stack overflow.
    // 256 words ≈ 1 KB; combined with chew_stack_and_work() it should overflow quickly.
    xTaskCreate(tiny_stack_task, "TinyStackTask", 256,  NULL, 4, NULL);

    ESP_LOGI(TAG, "Tasks started. Expect TWDT events and a stack overflow demo soon.");
}
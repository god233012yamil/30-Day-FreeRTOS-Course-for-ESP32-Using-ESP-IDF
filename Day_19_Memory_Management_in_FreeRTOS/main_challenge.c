/**
 * @file main.c
 * @brief Compare dynamic vs static FreeRTOS task creation and show heap impact.
 *
 * This example (ESP-IDF / ESP32-S3) creates two tasks:
 *   1) A dynamically allocated task using xTaskCreate().
 *   2) A statically allocated task using xTaskCreateStatic().
 *
 * It prints xPortGetFreeHeapSize() before and after each creation so you can
 * observe how dynamic creation reduces heap, while static creation does not.
 *
 * Key APIs demonstrated:
 *   - xTaskCreate(), xTaskCreateStatic()
 *   - xPortGetFreeHeapSize()
 *   - uxTaskGetStackHighWaterMark2()
 *
 * Notes:
 *   - In ESP-IDF, task stack size parameters are specified in **bytes**.
 *   - Use the logged stack high-water marks to tune stack sizes safely.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define TAG "task-compare"

// Use ~3 KB per task as a starting point; tune with the watermark logs.
#define DYN_STACK_BYTES   (3 * 1024)
#define STAT_STACK_BYTES  (3 * 1024)

// Static task storage
static StaticTask_t s_static_tcb;
static StackType_t  s_static_stack[STAT_STACK_BYTES];

// Handles so we can reference tasks if needed
static TaskHandle_t s_dyn_handle   = NULL;
static TaskHandle_t s_static_handle = NULL;

/**
 * @brief Simple worker task that logs its stack headroom.
 *
 * The task yields periodically and prints its minimum ever free stack space
 * (high-water mark) so you can right-size the stack.
 *
 * @param[in] pvParameters Unused.
 */
static void worker_task(void *pvParameters) {
    (void)pvParameters;
    for (;;) {
        UBaseType_t watermark = uxTaskGetStackHighWaterMark2(NULL);
        ESP_LOGI(TAG, "Task[%s] stack high-water mark: %u bytes",
                 pcTaskGetName(NULL), (unsigned)watermark);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/**
 * @brief Create one dynamic task and one static task, printing heap before/after.
 *
 * Steps:
 *   1) Print baseline free heap.
 *   2) Create dynamic task (xTaskCreate) and print free heap.
 *   3) Create static task (xTaskCreateStatic) and print free heap.
 * If a creation fails, an error is logged.
 */
void app_main(void) {
    // 1) Baseline
    size_t heap_before = xPortGetFreeHeapSize();
    ESP_LOGI(TAG, "Free heap BEFORE any task: %u bytes", (unsigned)heap_before);

    // 2) Create DYNAMIC task
    size_t heap_before_dyn = xPortGetFreeHeapSize();
    BaseType_t ok = xTaskCreate(
        worker_task,           // Task code
        "DynTask",             // Name
        DYN_STACK_BYTES,       // Stack size in BYTES (ESP-IDF)
        NULL,                  // Parameters
        5,                     // Priority
        &s_dyn_handle          // Handle out
    );
    size_t heap_after_dyn = xPortGetFreeHeapSize();

    if (ok != pdPASS || s_dyn_handle == NULL) {
        ESP_LOGE(TAG, "xTaskCreate (dynamic) FAILED");
    } else {
        ESP_LOGI(TAG, "Free heap BEFORE dynamic: %u, AFTER dynamic: %u (delta: -%d)",
                 (unsigned)heap_before_dyn, (unsigned)heap_after_dyn,
                 (int)(heap_before_dyn - heap_after_dyn));
    }

    // 3) Create STATIC task
    size_t heap_before_static = xPortGetFreeHeapSize();
    s_static_handle = xTaskCreateStatic(
        worker_task,           // Task code
        "StaticTask",          // Name
        STAT_STACK_BYTES,      // Stack size in BYTES (ESP-IDF)
        NULL,                  // Parameters
        5,                     // Priority
        s_static_stack,        // Stack buffer
        &s_static_tcb          // TCB buffer
    );
    size_t heap_after_static = xPortGetFreeHeapSize();

    if (s_static_handle == NULL) {
        ESP_LOGE(TAG, "xTaskCreateStatic (static) FAILED");
    } else {
        ESP_LOGI(TAG, "Free heap BEFORE static: %u, AFTER static: %u (delta: %d)",
                 (unsigned)heap_before_static, (unsigned)heap_after_static,
                 (int)(heap_before_static - heap_after_static));
    }

    // Final note for clarity
    ESP_LOGI(TAG, "Both tasks running. Adjust stack sizes using the watermark logs.");
}
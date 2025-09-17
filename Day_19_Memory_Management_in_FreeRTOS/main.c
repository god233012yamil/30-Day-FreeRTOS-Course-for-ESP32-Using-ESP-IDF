/**
 * @file main.c
 * @brief Demonstration of creating a FreeRTOS task with xTaskCreateStatic().
 *
 * This example shows how to create a FreeRTOS task in ESP-IDF using
 * statically allocated memory for both the task control block (TCB) and stack.
 * The task prints a log message every second, including the free stack space
 * (high-water mark), which is useful for tuning stack sizes.
 *
 * Key APIs demonstrated:
 *   - xTaskCreateStatic(): Create a task using statically allocated memory.
 *   - uxTaskGetStackHighWaterMark2(): Query the minimum free stack space ever
 *     available to the task (useful for stack tuning).
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define TAG "static-demo"

// Start around 2–3 KB for simple tasks that call library 
// code or printf(), then tune using 
// uxTaskGetStackHighWaterMark2()
#define STACK_SIZE_BYTES (3 * 1024)

// Task control block (TCB) for the task
StaticTask_t task_buffer;

// Stack memory for the task
StackType_t task_stack[STACK_SIZE_BYTES];

/**
 * @brief Task function that logs stack usage periodically.
 *
 * This task runs in an infinite loop, logging its available stack
 * space (high-water mark) once per second. This information can be
 * used to determine whether the stack size is sufficient or needs tuning.
 *
 * @param[in] pvParameters Pointer to optional parameters (unused).
 */
void static_task(void *pvParameters) {
    while (1) {
        ESP_LOGI(TAG, "Running static task; free stack (bytes): %u",
                 (unsigned)uxTaskGetStackHighWaterMark2(NULL));
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/**
 * @brief Main application entry point.
 *
 * This function creates the static task using xTaskCreateStatic(),
 * providing a statically allocated stack and TCB. If task creation
 * fails due to invalid parameters or insufficient memory, an error
 * message is logged.
 */
void app_main() {
    // Create task without using the heap
    TaskHandle_t handle = xTaskCreateStatic(
        static_task,      // Task function
        "StaticTask",     // Task name
        STACK_SIZE_BYTES,       // Stack size
        NULL,             // Parameters
        5,                // Priority
        task_stack,       // Stack memory
        &task_buffer      // Task control block
    );

    if (handle == NULL) {
        ESP_LOGE(TAG, 
            "xTaskCreateStatic failed (insufficient stack/invalid args)");
    }
}
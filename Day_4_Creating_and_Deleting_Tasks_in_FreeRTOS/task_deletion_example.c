/**
 * @file task_deletion_example.c
 * @brief Demonstrates task creation and deletion in FreeRTOS on ESP32.
 *
 * This example creates two tasks:
 *  1. hello_task – prints a counter every second and deletes itself after 5 iterations.
 *  2. control_task – waits 3 seconds, deletes hello_task if still running, then deletes itself.
 *
 * It illustrates both self-deletion and deleting another task by its handle.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Handle to the Hello Task
TaskHandle_t task_handle_hello = NULL;

/**
 * @brief Task that prints a counter and self-deletes after 5 iterations.
 *
 * Prints a message every second, increments a counter, and deletes itself
 * when the counter reaches 5.
 *
 * @param pvParameters Pointer to optional task parameters (unused here).
 */
void hello_task(void *pvParameters) {
    int counter = 0;
    while (1) {
        printf("Hello Task running, counter = %d\n", counter++);
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (counter >= 5) {
            printf("Hello Task deleting itself...\n");
            vTaskDelete(NULL); // Delete self
        }
    }
}

/**
 * @brief Task that deletes the hello_task after 3 seconds if still running.
 *
 * Waits for 3 seconds, checks if hello_task handle is valid,
 * deletes it if found, then deletes itself.
 *
 * @param pvParameters Pointer to optional task parameters (unused here).
 */
void control_task(void *pvParameters) {
    printf("Control Task running...\n");
    vTaskDelay(pdMS_TO_TICKS(3000));
    if (task_handle_hello != NULL) {
        printf("Control Task deleting Hello Task...\n");
        vTaskDelete(task_handle_hello); // Delete other task
    }
    vTaskDelete(NULL);
}

/**
 * @brief Main application entry point.
 *
 * Creates hello_task with priority 5 and control_task with priority 4.
 * The hello_task runs periodically and may be deleted either by itself
 * or by the control_task.
 */
void app_main() {
    // Create Hello Task
    xTaskCreate(
        hello_task,         // Task function
        "Hello Task",       // Name
        2048,               // Stack size (words)
        NULL,               // Parameters
        5,                  // Priority
        &task_handle_hello  // Handle
    );

    // Create Control Task
    xTaskCreate(
        control_task,       // Task function
        "Control Task",     // Name
        2048,               // Stack size (words)
        NULL,               // Parameters
        4,                  // Priority
        NULL                // Handle
    );
}
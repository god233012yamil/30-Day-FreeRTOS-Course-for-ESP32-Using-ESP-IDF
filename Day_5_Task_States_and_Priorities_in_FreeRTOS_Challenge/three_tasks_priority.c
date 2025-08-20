// https://chatgpt.com/c/6898a7f0-81b0-8333-a5de-a04208e005b6

/**
 * @file three_tasks_priority.c
 * @brief Example demonstrating FreeRTOS task priorities and dynamic priority change using ESP-IDF.
 *
 * This example creates three tasks with different priorities:
 *  - Low-priority task: Prints a message every 1 second.
 *  - Medium-priority task: Prints a message every 500 ms.
 *  - High-priority task: Starts with the highest priority, runs for 5 iterations,
 *    then lowers its own priority to the lowest level.
 *
 * The example illustrates:
 *  - Creating tasks with different priorities.
 *  - Using vTaskDelay for non-blocking delays.
 *  - Changing a task's own priority at runtime with vTaskPrioritySet.
 *
 * Target Platform: ESP32 with ESP-IDF
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Task handles so we can reference or modify tasks later
TaskHandle_t low_task_handle = NULL;
TaskHandle_t med_task_handle = NULL;
TaskHandle_t high_task_handle = NULL;

/**
 * @brief Low Priority Task - Executes periodically every 1 second.
 *
 * This task runs at the lowest priority (priority 1) and simply prints
 * a message once per second. It demonstrates a long-period task
 * that does not heavily load the CPU.
 *
 * @param pvParameter Not used in this example (pass NULL).
 */
void low_priority_task(void *pvParameter)
{
    while (1)
    {
        printf("Low Priority Task running every 1 second\n");
        vTaskDelay(pdMS_TO_TICKS(1000)); // Delay for 1s
    }
}

/**
 * @brief Medium Priority Task - Executes periodically every 500 ms.
 *
 * This task runs at medium priority (priority 2) and prints a message
 * twice per second. Its shorter delay means it will run more frequently
 * than the low-priority task when both are ready.
 *
 * @param pvParameter Not used in this example (pass NULL).
 */
void medium_priority_task(void *pvParameter)
{
    while (1)
    {
        printf("Medium Priority Task running every 500 ms\n");
        vTaskDelay(pdMS_TO_TICKS(500)); // Delay for 500 ms
    }
}

/**
 * @brief High Priority Task - Starts at highest priority, then lowers itself.
 *
 * This task runs first at the highest priority (priority 3) and performs
 * 5 iterations with short delays. After completing these iterations,
 * it lowers its own priority to the lowest level (priority 1) using
 * vTaskPrioritySet(). This demonstrates dynamic task priority adjustment.
 *
 * @param pvParameter Not used in this example (pass NULL).
 */
void high_priority_task(void *pvParameter)
{
    for (int i = 1; i <= 5; i++)
    {
        printf("High Priority Task iteration %d\n", i);
        vTaskDelay(pdMS_TO_TICKS(500)); // Short delay to simulate work
    }

    printf("High Priority Task lowering its priority to lowest...\n");
    vTaskPrioritySet(NULL, 1); // Change own priority to 1 (lowest)

    // Continue running normally after priority change
    while (1)
    {
        printf("High Priority Task (now low priority) still running...\n");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

/**
 * @brief Main application entry point for FreeRTOS tasks demonstration.
 *
 * Creates three tasks with different priorities:
 *  - Low priority (priority 1) task: Prints every 1 second.
 *  - Medium priority (priority 2) task: Prints every 500 ms.
 *  - High priority (priority 3) task: Runs 5 iterations, then reduces its own priority.
 *
 * The scheduler automatically manages which task runs based on priority and readiness.
 */
void app_main(void)
{
    // Create Low Priority Task (priority 1)
    xTaskCreate(low_priority_task, "LowPriorityTask", 2048, NULL, 1, &low_task_handle);

    // Create Medium Priority Task (priority 2)
    xTaskCreate(medium_priority_task, "MediumPriorityTask", 2048, NULL, 2, &med_task_handle);

    // Create High Priority Task (priority 3)
    xTaskCreate(high_priority_task, "HighPriorityTask", 2048, NULL, 3, &high_task_handle);
}
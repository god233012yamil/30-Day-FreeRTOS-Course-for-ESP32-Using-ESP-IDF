/**
 * @file day11_counting_semaphore.c
 * @brief ESP-IDF FreeRTOS demo using a counting semaphore to limit concurrent access to a shared resource.
 *
 * The program creates a counting semaphore with MAX_RESOURCES permits and spawns multiple
 * worker tasks. Each worker acquires a permit before simulating work and releases it after,
 * demonstrating controlled parallelism and back-pressure with xSemaphoreTake()/xSemaphoreGive().
 */

#include <stdio.h>
#include <stdint.h>               // for uintptr_t cast of pvParameters
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#define TAG "DAY11"
#define MAX_RESOURCES 3

// Counting semaphore representing the number of available resources.
SemaphoreHandle_t resource_sem;

/**
 * @brief Worker task that acquires a permit, simulates work, and then releases it.
 *
 * Each instance waits on the counting semaphore (blocking), prints when it acquires
 * a resource, simulates 2 seconds of work, prints when releasing, and then yields
 * briefly before repeating.
 *
 * @param pvParameters Task parameter carrying the worker ID (cast from void*).
 */
void worker_task(void *pvParameters) {
    int id = (int)(uintptr_t) pvParameters;

    while (1) {
        // Wait until a resource is available
        if (xSemaphoreTake(resource_sem, portMAX_DELAY)) {
            printf("Task %d acquired resource\n", id);

            // Simulate work with resource
            vTaskDelay(pdMS_TO_TICKS(2000));

            printf("Task %d releasing resource\n", id);

            // Release resource
            xSemaphoreGive(resource_sem);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/**
 * @brief Application entry point: creates the counting semaphore and worker tasks.
 *
 * Initializes a counting semaphore with MAX_RESOURCES permits (all initially available).
 * Spawns five worker tasks that compete for the three available resources to illustrate
 * concurrency throttling via a semaphore.
 */
void app_main() {
    // Create a counting semaphore with max 3 resources, initially all available
    resource_sem = xSemaphoreCreateCounting(MAX_RESOURCES, MAX_RESOURCES);
    if (resource_sem == NULL) {
        printf("Failed to create counting semaphore\n");
        return;
    }

    // Create 5 tasks competing for 3 resources
    for (int i = 1; i <= 5; i++) {
        char task_name[16];
        sprintf(task_name, "Worker%d", i);
        xTaskCreate(worker_task, task_name, 2048, (void *)(uintptr_t)i, 5, NULL);
    }
}
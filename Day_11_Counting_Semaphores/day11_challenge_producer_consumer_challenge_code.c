/*
How This Challenge Works:
    ■ Producer pushes events every 500 ms, regardless of consumer speed.
    ■ Consumers are slower (1 second each), so when both are busy, events accumulate in the semaphore’s count.
    ■ If consumers catch up, the queue drains.

    This demonstrates event buffering with counting semaphores, a key FreeRTOS 
    mechanism for managing producer-consumer patterns.

*/


/**
 * @file day11_challenge_producer_consumer.c
 * @brief ESP-IDF FreeRTOS challenge: producer-consumer with counting semaphore.
 *
 * Producer task simulates an ISR and gives a counting semaphore every 500 ms.
 * Two consumer tasks wait on the semaphore, consume it, and print messages.
 * Demonstrates how counting semaphores allow events to accumulate if
 * consumers are slower than producers.
 */

#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#define TAG "DAY11_CHALLENGE"

// Counting semaphore handle
SemaphoreHandle_t event_sem;

/**
 * @brief Producer task simulating an ISR event every 500 ms.
 *
 * The task "produces" events by giving the semaphore periodically.
 * In real systems, an ISR would call xSemaphoreGiveFromISR().
 */
void producer_task(void *pvParameters) {
    (void)pvParameters;

    while (1) {
        // Simulate ISR signaling every 500 ms
        xSemaphoreGive(event_sem);
        printf("Producer: Event generated\n");
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/**
 * @brief Consumer task that waits on the semaphore and processes events.
 *
 * Each consumer blocks until a semaphore token is available,
 * then prints a message simulating event handling.
 *
 * @param pvParameters Consumer ID (cast from void*).
 */
void consumer_task(void *pvParameters) {
    int id = (int)(uintptr_t)pvParameters;

    while (1) {
        // Wait until an event is available
        if (xSemaphoreTake(event_sem, portMAX_DELAY)) {
            printf("Consumer %d: Event consumed\n", id);

            // Simulate slower processing (1s)
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

/**
 * @brief Application entry point: sets up producer-consumer system.
 */
void app_main() {
    // Create a counting semaphore with max 10 tokens, starting empty
    event_sem = xSemaphoreCreateCounting(10, 0);
    if (event_sem == NULL) {
        printf("Failed to create counting semaphore\n");
        return;
    }

    // Create producer task
    xTaskCreate(producer_task, "Producer", 2048, NULL, 6, NULL);

    // Create two consumer tasks
    xTaskCreate(consumer_task, "Consumer1", 2048, (void *)(uintptr_t)1, 5, NULL);
    xTaskCreate(consumer_task, "Consumer2", 2048, (void *)(uintptr_t)2, 5, NULL);
}
/**
 * @file queue_producer_consumer.c
 * @brief ESP-IDF FreeRTOS example implementing a producer–consumer pattern via a queue.
 *
 * This demo creates a fixed-length queue of integers and two tasks:
 *  - producer_task: enqueues incrementing integers every 500 ms (100 ms send timeout).
 *  - consumer_task: dequeues and prints integers (1000 ms receive timeout).
 * It illustrates basic inter-task communication and back-pressure when the queue is full/empty.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#define QUEUE_LENGTH 5

/** @brief Global queue handle shared by producer and consumer tasks. */
QueueHandle_t queue;

/**
 * @brief Producer task that enqueues incrementing integers.
 *
 * Tries to send the current counter value to the queue with a 100 ms timeout.
 * On success, it prints the value and increments the counter; otherwise it logs
 * that the queue is full. Runs periodically every 500 ms.
 *
 * @param pvParameters Optional task parameter (unused).
 */
void producer_task(void *pvParameters) {
    (void)pvParameters;
    int count = 0;
    while (1) {
        if (xQueueSend(queue, &count, pdMS_TO_TICKS(100)) == pdPASS) {
            printf("Producer sent: %d\n", count);
            count++;
        } else {
            printf("Producer: Queue full!\n");
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/**
 * @brief Consumer task that dequeues integers and prints them.
 *
 * Waits up to 1000 ms for an item to arrive on the queue. If a value is
 * received it is printed; otherwise a "Queue empty" message is logged.
 * Runs continuously.
 *
 * @param pvParameters Optional task parameter (unused).
 */
void consumer_task(void *pvParameters) {
    (void)pvParameters;
    int value;
    while (1) {
        if (xQueueReceive(queue, &value, pdMS_TO_TICKS(1000)) == pdPASS) {
            printf("Consumer received: %d\n", value);
        } else {
            printf("Consumer: Queue empty!\n");
        }
    }
}

/**
 * @brief Application entry point: creates the queue and both tasks.
 *
 * Allocates a queue of length QUEUE_LENGTH to carry int items. If creation
 * succeeds, it spawns the producer and consumer tasks at priority 5; otherwise,
 * it logs a failure and returns.
 */
void app_main(void) {
    queue = xQueueCreate(QUEUE_LENGTH, sizeof(int));
    if (queue == NULL) {
        printf("Failed to create queue\n");
        return;
    }

    xTaskCreate(producer_task, "Producer", 2048, NULL, 5, NULL);
    xTaskCreate(consumer_task, "Consumer", 2048, NULL, 5, NULL);
}
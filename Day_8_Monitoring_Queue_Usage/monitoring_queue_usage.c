// https://chatgpt.com/c/68a09a51-fff8-8327-ad5d-a8be2d03edc0

/*
 * Example: Monitoring Queue Performance with uxQueueMessagesWaiting()
 *
 * This example demonstrates how to monitor the number of messages in a queue
 * using FreeRTOS API in ESP-IDF. A producer task writes integers into a queue,
 * while a consumer task reads them. A monitor task periodically checks the
 * number of items waiting in the queue using uxQueueMessagesWaiting().
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#define QUEUE_LENGTH 10
#define QUEUE_ITEM_SIZE sizeof(int)

static QueueHandle_t queue;

/**
 * @brief Producer task that generates numbers and pushes them into the queue.
 */
void producer_task(void *pvParameters) {
    int count = 0;
    while (1) {
        if (xQueueSend(queue, &count, pdMS_TO_TICKS(100)) == pdPASS) {
            printf("Producer: Sent %d\n", count);
            count++;
        } else {
            printf("Producer: Queue full!\n");
        }
        vTaskDelay(pdMS_TO_TICKS(200)); // simulate workload
    }
}

/**
 * @brief Consumer task that reads numbers from the queue.
 */
void consumer_task(void *pvParameters) {
    int value;
    while (1) {
        if (xQueueReceive(queue, &value, pdMS_TO_TICKS(500)) == pdPASS) {
            printf("Consumer: Received %d\n", value);
        } else {
            printf("Consumer: Queue empty!\n");
        }
        vTaskDelay(pdMS_TO_TICKS(300)); // simulate slower processing
    }
}

/**
 * @brief Monitor task that checks how many messages are waiting in the queue.
 */
void monitor_task(void *pvParameters) {
    while (1) {
        UBaseType_t waiting = uxQueueMessagesWaiting(queue);
        printf("Monitor: Queue has %lu messages waiting\n", (unsigned long)waiting);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/**
 * @brief Main entry point – initializes queue and tasks.
 */
void app_main(void) {
    // Create a queue to hold integers
    queue = xQueueCreate(QUEUE_LENGTH, QUEUE_ITEM_SIZE);

    if (queue == NULL) {
        printf("Failed to create queue!\n");
        return;
    }

    // Create tasks
    xTaskCreate(producer_task, "Producer", 2048, NULL, 2, NULL);
    xTaskCreate(consumer_task, "Consumer", 2048, NULL, 2, NULL);
    xTaskCreate(monitor_task, "Monitor", 2048, NULL, 1, NULL);
}
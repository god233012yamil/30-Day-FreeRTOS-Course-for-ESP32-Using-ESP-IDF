/**
 * @file main.c
 * @brief Day 25 – Inter-Task Communication demo on ESP32 (ESP-IDF + FreeRTOS).
 *
 * @details
 * This example demonstrates multiple FreeRTOS inter-task communication
 * primitives working together:
 *
 *  - **Queue** (`sensor_queue`) to pass integer temperature samples
 *    from a producer (sensor_task) to a consumer (processor_task).
 *  - **Counting Semaphore** (`sample_sem`) used as a "new-sample" signal
 *    so the consumer only attempts to read when data is expected.
 *  - **Mutex** (`uart_mutex`) to serialize access to the UART/stdout
 *    from the logger task (prevents interleaved prints).
 *  - **Event Group** (`system_event_group`) to advertise system state:
 *    sensor active and processor ready/active. The logger waits for both.
 *
 * Flow:
 *  1) `sensor_task` produces a fake temperature every 1 s, sends it to the
 *     queue, gives the counting semaphore, and sets BIT_SENSOR_READY.
 *  2) `processor_task` waits on the semaphore, reads from the queue,
 *     accumulates five samples, computes an average, logs it, and sets
 *     BIT_PROCESSOR_READY.
 *  3) `logger_task` waits (ALL bits) for both sensor and processor readiness
 *     and then (protected by a mutex) prints a status line every 2 s.
 *
 * @note This code targets ESP-IDF (ESP32). Ensure logging is enabled and
 *       FreeRTOS is available (default on ESP-IDF).
 * @copyright
 *  MIT-style usage intended for educational purposes.
 */
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "driver/uart.h"
#include "esp_log.h"

#define TAG "DAY25"

// Event group bits
#define BIT_SENSOR_READY    (1 << 0)
#define BIT_PROCESSOR_READY (1 << 1)

// Handles
QueueHandle_t sensor_queue;
SemaphoreHandle_t sample_sem;
SemaphoreHandle_t uart_mutex;
EventGroupHandle_t system_event_group;

/**
 * @brief Producer task that simulates a temperature sensor.
 *
 * @param[in] pvParameters Unused (pass NULL).
 *
 * @details
 * Periodically (every 1000 ms) generates a pseudo-random temperature in the
 * range [20, 29] °C, sends it to `sensor_queue`, signals new data via the
 * counting semaphore `sample_sem`, and sets `BIT_SENSOR_READY` in the
 * event group. Runs indefinitely.
 *
 * @pre `sensor_queue`, `sample_sem`, and `system_event_group` must be created.
 */
void sensor_task(void *pvParameters) {
    int temp = 25;
    while (1) {
        temp = 20 + rand() % 10; // fake data
        xQueueSend(sensor_queue, &temp, portMAX_DELAY);
        xSemaphoreGive(sample_sem); // signal new sample
        xEventGroupSetBits(system_event_group, BIT_SENSOR_READY);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/**
 * @brief Consumer/processor task that averages temperature samples.
 *
 * @param[in] pvParameters Unused (pass NULL).
 *
 * @details
 * Waits on `sample_sem` indicating new data is likely available, then
 * attempts a non-blocking read from `sensor_queue`. Accumulates 5 samples,
 * computes the average, logs it, resets its accumulator, and sets
 * `BIT_PROCESSOR_READY` in the event group (to signal readiness/activity).
 * Runs indefinitely.
 *
 * @pre `sensor_queue`, `sample_sem`, and `system_event_group` must be created.
 */
void processor_task(void *pvParameters) {
    int value;
    int sum = 0, count = 0;

    while (1) {
        if (xSemaphoreTake(sample_sem, portMAX_DELAY)) {
            if (xQueueReceive(sensor_queue, &value, 0)) {
                sum += value;
                count++;
                if (count == 5) {
                    int avg = sum / count;
                    sum = 0;
                    count = 0;

                    ESP_LOGI(TAG, "Computed avg temp: %d", avg);
                    xEventGroupSetBits(system_event_group, BIT_PROCESSOR_READY);
                }
            }
        }
    }
}

/**
 * @brief Logger task that prints a message once both producer and processor are active.
 *
 * @param[in] pvParameters Unused (pass NULL).
 *
 * @details
 * Waits (atomically) for both `BIT_SENSOR_READY` and `BIT_PROCESSOR_READY`
 * in `system_event_group` (wait-for-all). When both are set, it takes the
 * `uart_mutex` to serialize console output, prints a status message, releases
 * the mutex, and then delays for 2000 ms before checking again.
 * Runs indefinitely.
 *
 * @pre `system_event_group` and `uart_mutex` must be created.
 */
void logger_task(void *pvParameters) {
    while (1) {
        EventBits_t bits = xEventGroupWaitBits(
            system_event_group,
            BIT_SENSOR_READY | BIT_PROCESSOR_READY,
            pdFALSE, // don’t clear bits
            pdTRUE,  // wait for ALL
            portMAX_DELAY
        );

        if ((bits & (BIT_SENSOR_READY | BIT_PROCESSOR_READY)) ==
            (BIT_SENSOR_READY | BIT_PROCESSOR_READY)) {
            if (xSemaphoreTake(uart_mutex, portMAX_DELAY)) {
                printf("Logger: Both Sensor & Processor are active\n");
                xSemaphoreGive(uart_mutex);
            }
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }
}

/**
 * @brief Application entry point (ESP-IDF).
 *
 * @details
 * Creates the required FreeRTOS objects (queue, counting semaphore, mutex,
 * event group) and spawns three tasks:
 *  - `sensor_task` (priority 5)
 *  - `processor_task` (priority 6)
 *  - `logger_task` (priority 4)
 *
 * On success, logs that the system has initialized.
 *
 * @post All tasks are running and the RTOS scheduler takes over.
 */
void app_main() {
    // Create a queue for sensor data
    sensor_queue = xQueueCreate(10, sizeof(int));

    // Counting semaphore for samples
    sample_sem = xSemaphoreCreateCounting(10, 0);

    // Mutex for UART access
    uart_mutex = xSemaphoreCreateMutex();

    // Event group for system state
    system_event_group = xEventGroupCreate();

    // Create tasks
    xTaskCreate(sensor_task, "SensorTask", 2048, NULL, 5, NULL);
    xTaskCreate(processor_task, "ProcessorTask", 2048, NULL, 6, NULL);
    xTaskCreate(logger_task, "LoggerTask", 2048, NULL, 4, NULL);

    ESP_LOGI(TAG, "Day 25 system initialized");
}
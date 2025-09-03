#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

// Mutex for resource sharing
SemaphoreHandle_t uart_mutex;

// Low priority task that holds a resource
void low_task(void *pv) {
    while (1) {
        if (xSemaphoreTake(uart_mutex, portMAX_DELAY)) {
            printf("Low task acquired UART\n");
            vTaskDelay(pdMS_TO_TICKS(2000)); // Hold resource
            printf("Low task releasing UART\n");
            xSemaphoreGive(uart_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// High priority task that needs the same resource
void high_task(void *pv) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(500)); // Start later
        if (xSemaphoreTake(uart_mutex, portMAX_DELAY)) {
            printf("High task using UART\n");
            vTaskDelay(pdMS_TO_TICKS(500));
            xSemaphoreGive(uart_mutex);
        }
    }
}

// Medium priority task to demonstrate priority inversion
void medium_task(void *pv) {
    while (1) {
        printf("Medium task running\n");
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}

// Entry point 
void app_main() {
    // Create the mutex
    uart_mutex = xSemaphoreCreateMutex();

    // Create tasks with different priorities
    xTaskCreate(low_task, "LowTask", 2048, NULL, 2, NULL);
    xTaskCreate(high_task, "HighTask", 2048, NULL, 10, NULL);
    xTaskCreate(medium_task, "MedTask", 2048, NULL, 5, NULL);
}
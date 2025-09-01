#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/uart.h"

#define TAG "DAY12"
#define UART_PORT UART_NUM_0

SemaphoreHandle_t uart_mutex;

void uart_task(void *pvParameter) {
    const char *task_name = (const char *) pvParameter;
    while (1) {
        // Try to take the mutex before accessing simulated UART
        if (xSemaphoreTake(uart_mutex, portMAX_DELAY)) {
            for (int i = 0; i < 5; i++) {
                // Simulating UART write
                printf("%s writing line %d\n", task_name, i);
                vTaskDelay(pdMS_TO_TICKS(200));
            }
            // Release the mutex
            xSemaphoreGive(uart_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main() {
    // Create mutex
    uart_mutex = xSemaphoreCreateMutex();
    if (uart_mutex == NULL) {
        printf("Failed to create mutex\n");
        return;
    }

    // Create two tasks sharing UART
    xTaskCreate(uart_task, "TaskA", 2048, "Task A", 5, NULL);
    xTaskCreate(uart_task, "TaskB", 2048, "Task B", 5, NULL);
}
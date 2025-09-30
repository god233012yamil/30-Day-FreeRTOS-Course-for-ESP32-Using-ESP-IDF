/**
 * @file main.c
 * 
 * @author Yamil Garcia
 * 
 * @brief This code demonstrates the usage of FreeRTOS task management functions to debug and monitor tasks.
 * 
 * @version 0.1
 * @date 2025-09-21
 * 
 * @note This project requires to enable the runtime stats collection in FreeRTOSConfig.h:
 *       KCONFIG Name: FREERTOS_GENERATE_RUN_TIME_STATS
 * 
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Example task to demonstrate usage of FreeRTOS task functions
void debug_task(void *pvParameters) {
    while (1) {
        // Print stack high-water mark
        UBaseType_t watermark = uxTaskGetStackHighWaterMark(NULL);
        printf("DebugTask high-water mark: %u words\n", watermark);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Main application entry point
void app_main() {
    // Create the debug task
    xTaskCreate(debug_task, "DebugTask", 2048, NULL, 5, NULL);

    // Example: Print runtime stats periodically
    char buffer[512];
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        vTaskList(buffer);
        printf("Task List:\n%s\n", buffer);

        vTaskGetRunTimeStats(buffer);
        printf("Runtime Stats:\n%s\n", buffer);
    }
}
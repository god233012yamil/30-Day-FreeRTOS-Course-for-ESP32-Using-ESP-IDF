/*
Example – Synchronizing System Readiness Flags (Wi-Fi + MQTT)

This example shows how a task can wait for both Wi-Fi and MQTT 
to be connected before proceeding.

Explanation:
■ wifi_task() and mqtt_task() set their respective bits after delays.
■ controller_task() waits until both bits are set before proceeding.
■ This ensures the main logic runs only when the system is fully ready.
*/

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"

#define WIFI_CONNECTED_BIT   BIT0
#define MQTT_CONNECTED_BIT   BIT1

static EventGroupHandle_t app_event_group;
static const char *TAG = "EVENT_EX1";

// Simulated Wi-Fi connection
void wifi_task(void *pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(2000)); // simulate delay
    ESP_LOGI(TAG, "Wi-Fi connected");

    // Set the WIFI_CONNECTED_BIT
    xEventGroupSetBits(app_event_group, WIFI_CONNECTED_BIT);
    
    // Delete the task as it's no longer needed
    vTaskDelete(NULL);
}

// Simulated MQTT connection
void mqtt_task(void *pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(4000)); // simulate delay
    ESP_LOGI(TAG, "MQTT connected");

    // Set the MQTT_CONNECTED_BIT
    xEventGroupSetBits(app_event_group, MQTT_CONNECTED_BIT);

    // Delete the task as it's no longer needed
    vTaskDelete(NULL);
}

// Main controller waits for Wi-Fi + MQTT
void controller_task(void *pvParameters) {
    ESP_LOGI(TAG, "Waiting for Wi-Fi + MQTT...");
    
    // Wait for both WIFI_CONNECTED_BIT and MQTT_CONNECTED_BIT to be set
    xEventGroupWaitBits(
        app_event_group,
        WIFI_CONNECTED_BIT | MQTT_CONNECTED_BIT,
        pdTRUE,   // clear bits on exit
        pdTRUE,   // wait for ALL bits
        portMAX_DELAY
    );
    ESP_LOGI(TAG, "System is ready. Starting application logic...");
    for (;;) {
        ESP_LOGI(TAG, "Controller: running...");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// Main application entry point
void app_main(void) {
    // Create event group
    app_event_group = xEventGroupCreate();

    // Create tasks
    xTaskCreate(wifi_task, "wifi_task", 2048, NULL, 2, NULL);
    xTaskCreate(mqtt_task, "mqtt_task", 2048, NULL, 2, NULL);
    xTaskCreate(controller_task, "controller_task", 2048, NULL, 2, NULL);
}
/*
Example 2 – Broadcasting Events from a GPIO ISR to Multiple Tasks

In this example, pressing a button wakes two tasks using the same event bit.
*/
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define BUTTON_GPIO         GPIO_NUM_0
#define BUTTON_PRESSED_BIT  BIT2

static EventGroupHandle_t app_event_group;
static const char *TAG = "EVENT_EX2";

// ISR for button press
static void IRAM_ATTR button_isr_handler(void *arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // Set the event bit to notify tasks
    xEventGroupSetBitsFromISR(app_event_group, BUTTON_PRESSED_BIT, &xHigherPriorityTaskWoken);    
    // Yield to a higher priority task if needed
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

// Task A: Listens for button press events
void button_listener_task_A(void *pvParameters) {
    for (;;) {
        // Wait for the button pressed event
        xEventGroupWaitBits(
            app_event_group, 
            BUTTON_PRESSED_BIT, 
            pdTRUE,     // clear bits on exit
            pdFALSE,    // wait for any bit
            portMAX_DELAY
        );
        ESP_LOGI(TAG, "[Task A] Button Pressed!");
    }
}

// Task B: Listens for button press events
void button_listener_task_B(void *pvParameters) {
    for (;;) {
        // Wait for the button pressed event
        xEventGroupWaitBits(
            app_event_group, 
            BUTTON_PRESSED_BIT, 
            pdTRUE,     // clear bits on exit 
            pdFALSE,    // wait for any bit   
            portMAX_DELAY
        );
        ESP_LOGI(TAG, "[Task B] Button Pressed!");
    }
}

// Main application entry point
void app_main(void) {
    // Create event group
    app_event_group = xEventGroupCreate();

    // Configure button
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1,
        .intr_type = GPIO_INTR_NEGEDGE,
    };

    // Initialize GPIO and ISR
    gpio_config(&io_conf);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_GPIO, button_isr_handler, NULL);

    // Create tasks
    xTaskCreate(button_listener_task_A, "taskA", 2048, NULL, 2, NULL);
    xTaskCreate(button_listener_task_B, "taskB", 2048, NULL, 2, NULL);
}
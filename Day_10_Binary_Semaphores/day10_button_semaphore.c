/**
 * @file day10_button_semaphore.c
 * @brief ESP-IDF example: button ISR signals a binary semaphore, task waits and handles the press.
 *
 * The GPIO interrupt service routine (ISR) gives a binary semaphore using
 * xSemaphoreGiveFromISR(). A FreeRTOS task blocks on that semaphore via
 * xSemaphoreTake() and logs the button press, where debouncing or actions can be added.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define BUTTON_GPIO 0   // BOOT button on many ESP32 boards
#define TAG "DAY10"

/** @brief Binary semaphore signaled from the ISR and taken by the button task. */
SemaphoreHandle_t button_semaphore;

/**
 * @brief GPIO interrupt service routine; gives the semaphore to wake the waiting task.
 *
 * Called on the configured edge for BUTTON_GPIO. Uses xSemaphoreGiveFromISR() and,
 * if a higher-priority task is unblocked, requests an immediate context switch.
 *
 * @param arg Optional ISR argument (unused).
 */
static void IRAM_ATTR button_isr_handler(void *arg) {
    (void)arg; // Unused
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // Give semaphore from ISR
    xSemaphoreGiveFromISR(button_semaphore, &xHigherPriorityTaskWoken);

    // If a higher-priority task was woken, yield immediately
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

/**
 * @brief Task that blocks on the semaphore and handles button presses.
 *
 * Waits indefinitely for the semaphore given by the ISR. When taken, logs the
 * event and serves as a placeholder for debouncing or application logic.
 *
 * @param pvParameter Optional task parameter (unused).
 */
void button_task(void *pvParameter) {
    (void)pvParameter; // Unused
    while (1) {
        if (xSemaphoreTake(button_semaphore, portMAX_DELAY)) {
            ESP_LOGI(TAG, "Button pressed!");
            // Do work here (debounce, action, etc.)
        }
    }
}

/**
 * @brief Application entry point: sets up GPIO, ISR, semaphore, and the task.
 *
 * Creates a binary semaphore, configures BUTTON_GPIO as input with pull-up and
 * falling-edge interrupt, installs/attaches the ISR, and starts the button task.
 */
void app_main() {
    // Create binary semaphore
    button_semaphore = xSemaphoreCreateBinary();
    if (button_semaphore == NULL) {
        ESP_LOGE(TAG, "Failed to create semaphore");
        return;
    }

    // Configure button pin
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE,  // Falling edge
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE
    };
    gpio_config(&io_conf);

    // Install ISR service and attach handler
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_GPIO, button_isr_handler, NULL);

    // Create task to handle button events
    xTaskCreate(button_task, "ButtonTask", 2048, NULL, 10, NULL);
}
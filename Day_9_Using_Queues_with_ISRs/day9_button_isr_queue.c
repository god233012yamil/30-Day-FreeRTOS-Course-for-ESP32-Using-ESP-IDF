/**
 * @file day9_button_isr_queue.c
 * @brief ESP-IDF example: handle a GPIO button press in an ISR and pass events to a task via a queue.
 *
 * The ISR captures the GPIO number on a falling edge and sends it to a FreeRTOS queue using
 * xQueueSendFromISR(). A consumer task blocks on the queue and logs debounced button events.
 * This pattern decouples fast, ISR-safe work from slower processing in task context.
 *
 * Hardware:
 *  - BUTTON_GPIO (default: GPIO0) pulled up; button connects GPIO0 to GND on press.
 */

#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define BUTTON_GPIO 0   // Example: BOOT button on many ESP32 boards
#define TAG "DAY9"

QueueHandle_t button_queue;

/**
 * @brief GPIO interrupt service routine; sends the GPIO number to a queue.
 *
 * Called on the configured edge. Uses xQueueSendFromISR() to enqueue the pin number
 * and conditionally yields to a higher-priority task that was woken by the send.
 *
 * @param arg ISR argument carrying the GPIO number (cast from void*).
 */
static void IRAM_ATTR button_isr_handler(void *arg) {
    uint32_t gpio_num = (uint32_t) arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // Send GPIO number to the queue (non-blocking, ISR safe)
    xQueueSendFromISR(button_queue, &gpio_num, &xHigherPriorityTaskWoken);

    // If a higher-priority task was woken, yield immediately
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

/**
 * @brief Task that waits for button events and processes them.
 *
 * Blocks on the queue for new GPIO numbers posted by the ISR. When an event
 * arrives, logs the GPIO and provides a place to implement debouncing or
 * application-specific handling.
 *
 * @param pvParameter Optional task parameter (unused).
 */
void button_task(void *pvParameter) {
    (void)pvParameter;
    uint32_t io_num;
    while (1) {
        // Wait indefinitely until ISR sends a message
        if (xQueueReceive(button_queue, &io_num, portMAX_DELAY)) {
            ESP_LOGI(TAG, "Button pressed on GPIO %" PRIu32, io_num);

            // Debounce or handle logic here
        }
    }
}

/**
 * @brief Application entry point: sets up GPIO, ISR, queue, and the consumer task.
 *
 * Creates a queue for button events, configures BUTTON_GPIO as input with pull-up
 * and falling-edge interrupt, installs the ISR service, attaches the ISR handler,
 * and starts a task to consume and process button events.
 */
void app_main() {
    // Create queue for GPIO events
    button_queue = xQueueCreate(10, sizeof(uint32_t));
    if (button_queue == NULL) {
        printf("Failed to create queue\n");
        return;
    }

    // Configure button pin
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE,  // Trigger on falling edge
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE
    };
    gpio_config(&io_conf);

    // Install ISR service
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_GPIO, button_isr_handler, (void *) BUTTON_GPIO);

    // Start button processing task
    xTaskCreate(button_task, "ButtonTask", 2048, NULL, 10, NULL);
}
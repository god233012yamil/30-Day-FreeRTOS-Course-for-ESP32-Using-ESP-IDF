/**
 * @file dual_button_isr_queue.c
 * @brief Example demonstrating how to use a FreeRTOS queue to pass GPIO
 *        interrupt events (button presses) from an ISR to a task on ESP32
 *        using the ESP-IDF framework.
 *
 * This code configures two buttons on different GPIOs. Both buttons share
 * the same ISR, which posts the GPIO number of the pressed button to a queue.
 * A FreeRTOS task consumes queue messages, identifies which button was pressed,
 * and prints a different message accordingly. A basic debounce mechanism is
 * also included in the task for reliable operation.
 * 
 * Notes & practical tips:
 *  1. Wiring assumes active-low buttons (button shorts GPIO→GND when pressed) with internal 
 *     pull-ups enabled; invert the interrupt type/pulls if your hardware is active-high.
 *  2. The ISR posts the GPIO number to the queue. By registering the same ISR twice with 
 *     different arg values, you naturally support any number of buttons with one handler.
 *  3. Debounce here is intentionally simple and done in the task (not in the ISR) to keep 
 *     the ISR short. Tune DEBOUNCE_TICKS to your hardware.
 *  4. If you later need long ISR safety (e.g., flash logging), keep only the minimal 
 *     xQueueSendFromISR() in the ISR and do all work in the task—exactly as shown.
 * 
 */
#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define TAG            "DAY9"

// GPIO definitions for two buttons
#define BUTTON_GPIO1   0   ///< Example: BOOT button on many ESP32 boards
#define BUTTON_GPIO2   4   ///< Example: an auxiliary button on GPIO4

// Debounce time in RTOS ticks (30 ms)
#define DEBOUNCE_TICKS pdMS_TO_TICKS(30)

// Queue handle for button events
static QueueHandle_t button_queue;

/**
 * @brief ISR handler for GPIO interrupts.
 *
 * This ISR is registered for both buttons. It captures the GPIO number of
 * the pin that triggered the interrupt and sends it to the FreeRTOS queue.
 * The ISR is kept minimal: only queue posting and ISR-to-task context switching.
 *
 * @param arg Pointer cast to the GPIO number that generated the interrupt.
 */
static void IRAM_ATTR button_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // Send the GPIO number to the queue (ISR-safe, non-blocking)
    xQueueSendFromISR(button_queue, &gpio_num, &xHigherPriorityTaskWoken);

    // If a higher-priority task was unblocked, yield immediately
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

/**
 * @brief Task to process button press events.
 *
 * This task continuously waits on the button queue for new messages posted by
 * the ISR. It identifies which button was pressed and prints a different
 * message for each. A simple debounce mechanism prevents spurious multiple
 * triggers caused by switch bouncing.
 *
 * @param pvParameter Unused parameter (required by FreeRTOS task signature).
 */
static void button_task(void *pvParameter)
{
    uint32_t io_num;

    // Track last accepted tick for each button (for debouncing)
    TickType_t last_tick_btn1 = 0;
    TickType_t last_tick_btn2 = 0;

    for (;;) {
        if (xQueueReceive(button_queue, &io_num, portMAX_DELAY)) {
            TickType_t now = xTaskGetTickCount();
            bool accepted = true;

            // Simple per-button debounce check
            if (io_num == BUTTON_GPIO1) {
                if ((now - last_tick_btn1) < DEBOUNCE_TICKS) accepted = false;
                else last_tick_btn1 = now;
            } else if (io_num == BUTTON_GPIO2) {
                if ((now - last_tick_btn2) < DEBOUNCE_TICKS) accepted = false;
                else last_tick_btn2 = now;
            }

            if (!accepted) continue;

            // Print message based on which button was pressed
            switch (io_num) {
                case BUTTON_GPIO1:
                    ESP_LOGI(TAG, "BUTTON 1 pressed on GPIO %" PRIu32 " (BOOT).", io_num);
                    break;

                case BUTTON_GPIO2:
                    ESP_LOGI(TAG, "BUTTON 2 pressed on GPIO %" PRIu32 " (AUX).", io_num);
                    break;

                default:
                    ESP_LOGW(TAG, "Unexpected GPIO event on %" PRIu32 ".", io_num);
                    break;
            }
        }
    }
}

/**
 * @brief Application entry point.
 *
 * Configures two GPIO pins as inputs with falling-edge interrupts (active-low
 * buttons with pull-ups). Installs the ISR service, attaches the button ISR
 * to both pins, and starts the task that processes button events.
 */
void app_main(void)
{
    // Create a queue capable of holding up to 10 button events
    button_queue = xQueueCreate(10, sizeof(uint32_t));
    configASSERT(button_queue != NULL);

    // Configure both GPIOs at once with a bitmask
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE,             // falling edge (active-low)
        .mode = GPIO_MODE_INPUT,                    // input mode
        .pin_bit_mask = (1ULL << BUTTON_GPIO1) | (1ULL << BUTTON_GPIO2),
        .pull_up_en = GPIO_PULLUP_ENABLE,           // enable internal pull-up
        .pull_down_en = GPIO_PULLDOWN_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    // Install ISR service
    ESP_ERROR_CHECK(gpio_install_isr_service(0));

    // Add both buttons to ISR handler
    ESP_ERROR_CHECK(gpio_isr_handler_add(BUTTON_GPIO1, button_isr_handler, (void *)BUTTON_GPIO1));
    ESP_ERROR_CHECK(gpio_isr_handler_add(BUTTON_GPIO2, button_isr_handler, (void *)BUTTON_GPIO2));

    // Create the button processing task
    xTaskCreate(button_task, "ButtonTask", 2048, NULL, 10, NULL);

    ESP_LOGI(TAG, "Button ISR/Task setup complete. Listening on GPIO %" PRIu32 " and %" PRIu32 ".",
             (uint32_t)BUTTON_GPIO1, (uint32_t)BUTTON_GPIO2);
}
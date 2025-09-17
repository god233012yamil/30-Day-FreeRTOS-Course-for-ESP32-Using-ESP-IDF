/**
 * @file main.c
 * @brief ESP32 example: safely notify a FreeRTOS task from GPIO ISRs (notification + queue).
 *
 * @details
 * This extended example shows two safe ISR→task communication patterns:
 *
 * 1) Button 1: an ISR uses `vTaskNotifyGiveFromISR()` to wake a task.
 * 2) Button 2: a separate ISR sends its GPIO number through a FreeRTOS queue
 *    using `xQueueSendFromISR()`.
 *
 * The task blocks in a loop, servicing either source and handling them
 * differently (e.g., button 1 as a "short press" and button 2 as a "long press").
 * The code keeps ISR work minimal and defers all logging/processing to the task.
 *
 * @note
 *  - Only call ISR-safe APIs inside ISRs.
 *  - Keep ISRs short to minimize latency.
 *  - Adjust GPIOs and pull configuration to your hardware.
 */

#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"

/** GPIO numbers for the two buttons. */
#define BUTTON1_GPIO 0   ///< Button 1 uses task notification (short press)
#define BUTTON2_GPIO 2   ///< Button 2 uses a queue message (long press)

/** Log tag used by ESP-IDF logging macros. */
#define TAG "BTN_DEMO"

/**
 * @brief Handle to the task that processes button events.
 *
 * The Button 1 ISR notifies this task using `vTaskNotifyGiveFromISR()`.
 */
static TaskHandle_t button_task_handle = NULL;

/**
 * @brief Queue used by Button 2 ISR to deliver GPIO events to the task.
 *
 * Each queue item is a `uint32_t` containing the GPIO number that fired.
 */
static QueueHandle_t gpio_evt_queue = NULL;

/**
 * @brief ISR for Button 1: notify the task.
 *
 * @param[in] arg Unused user argument.
 *
 * @details
 * Uses `vTaskNotifyGiveFromISR()` to increment the task's notification count.
 * If a higher-priority task is unblocked, request a context switch on ISR exit.
 */
static void IRAM_ATTR button1_isr_handler(void *arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // Notify the task that Button 1 was pressed
    vTaskNotifyGiveFromISR(button_task_handle, &xHigherPriorityTaskWoken);
    
    // Request a context switch if a higher-priority task was woken
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

/**
 * @brief ISR for Button 2: send the GPIO number via a queue.
 *
 * @param[in] arg Pointer whose value is the GPIO number (cast to `void*`).
 *
 * @details
 * Sends a `uint32_t` containing the GPIO number to `gpio_evt_queue` using the
 * ISR-safe `xQueueSendFromISR()` API. Requests a context switch if needed.
 */
static void IRAM_ATTR button2_isr_handler(void *arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // Retrieve the GPIO number from the argument
    uint32_t gpio_num = (uint32_t)(uintptr_t)arg;  // value passed at registration

    // Send the GPIO number to the queue
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, &xHigherPriorityTaskWoken);
    
    // Request a context switch if a higher-priority task was woken
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

/**
 * @brief Task that handles button events from both ISRs.
 *
 * @param[in] pvParameter Unused (conventional FreeRTOS task signature).
 *
 * @details
 * The task services two sources in a single loop:
 *  - Button 1: waits for a task notification (treated as a "short press").
 *  - Button 2: receives GPIO numbers from `gpio_evt_queue` (treated as a
 *    "long press").
 *
 * Implementation notes:
 *  - The loop checks for a notification with a finite timeout, then drains any
 *    queued GPIO events. This keeps the task responsive to both sources
 *    without busy-waiting.
 */
static void button_task(void *pvParameter) {
    (void)pvParameter;

    for (;;) {
        // Wait up to 50 ms for a Button 1 notification
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50)) > 0) {
            // Handle Button 1: "short press" action
            ESP_LOGI(TAG, "Button 1 short press (notification)");
        }

        // Drain any queued Button 2 events
        uint32_t gpio_num;
        while (xQueueReceive(gpio_evt_queue, &gpio_num, 0) == pdTRUE) {
            if (gpio_num == BUTTON2_GPIO) {
                // Handle Button 2: "long press" action
                ESP_LOGI(TAG, "Button 2 long press (queue) on GPIO %" PRIu32, gpio_num);
            } else {
                // Generic handling for any unexpected GPIO
                ESP_LOGW(TAG, "Unhandled GPIO event on %" PRIu32, gpio_num);
            }
        }
    }
}

/**
 * @brief Configure a GPIO as an input with falling-edge interrupt and pull-ups.
 *
 * @param[in] gpio_num The GPIO number to configure.
 *
 * @details
 * Utility helper to apply a consistent configuration to both button pins.
 */
static void configure_button_gpio(gpio_num_t gpio_num) {
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << gpio_num),
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&io_conf);
}

/**
 * @brief Application entry point executed by ESP-IDF after startup.
 *
 * @details
 * - Configures two button GPIOs for falling-edge interrupts with pull-ups.
 * - Creates the button task and a queue for GPIO events.
 * - Installs the ISR service and registers both ISR handlers.
 */
void app_main(void) {
    // Configure button pins
    configure_button_gpio(BUTTON1_GPIO);
    configure_button_gpio(BUTTON2_GPIO);

    // Create queue for Button 2 events (depth 8 x uint32_t)
    gpio_evt_queue = xQueueCreate(8, sizeof(uint32_t));

    // Create the handler task
    xTaskCreate(button_task, "ButtonTask", 2048, NULL, 10, &button_task_handle);

    // Install ISR service (no flags)
    gpio_install_isr_service(0);

    // Register ISR handlers
    gpio_isr_handler_add(BUTTON1_GPIO, button1_isr_handler, NULL);
    gpio_isr_handler_add(BUTTON2_GPIO, button2_isr_handler, (void *)(uintptr_t)BUTTON2_GPIO);
}
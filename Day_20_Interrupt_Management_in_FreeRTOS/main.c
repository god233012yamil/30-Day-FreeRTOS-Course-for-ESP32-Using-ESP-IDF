/**
* @file main.c
* @brief ESP32 example: safely notify a FreeRTOS task from a GPIO ISR.
*
* @details
* This example demonstrates a minimal, safe pattern to communicate an event
* from an interrupt service routine (ISR) to a regular FreeRTOS task using
* task notifications. A falling edge on the configured button GPIO triggers
* an ISR, which then uses `vTaskNotifyGiveFromISR()` to notify the task. The
* task blocks indefinitely on `ulTaskNotifyTake()` and logs a message each
* time the button is pressed.
*
* The pattern shown here avoids unsafe operations in ISRs and ensures a
* context switch occurs immediately when a higher-priority task is unblocked.
*
* @note
* - Only use ISR-safe FreeRTOS APIs inside ISRs (e.g., `vTaskNotifyGiveFromISR`).
* - Keep ISR work minimal; defer substantial work to a task.
* - Configure proper pull resistors for your board and button wiring.
*
* @copyright
* (c) 2025. Provided as an educational example without warranty.
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

// GPIO number where the momentary button is connected.
#define BUTTON_GPIO 0

// Log tag used by ESP-IDF logging macros.
#define TAG "DAY20"

/**
* @brief Handle to the task that processes button press events.
*
* This handle is assigned when the button task is created and is used by the
* ISR to deliver task notifications.
*/
TaskHandle_t button_task_handle = NULL;

/**
* @brief GPIO interrupt service routine (ISR) for the button.
*
* @details
* Invoked on a configured falling-edge interrupt from the button GPIO. Uses
* `vTaskNotifyGiveFromISR()` to notify the button task that an event occurred.
* If the notification wakes a higher-priority task, `portYIELD_FROM_ISR()` is
* called to request a context switch as soon as the ISR exits.
*
* @param[in] arg Unused user argument passed at ISR registration time.
*
* @warning Do not call non-ISR-safe functions here. Keep ISR execution time
* as short as possible to minimize interrupt latency.
*/
static void IRAM_ATTR button_isr_handler(void *arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // Notify the task that a button press occurred
    vTaskNotifyGiveFromISR(button_task_handle, 
                           &xHigherPriorityTaskWoken);

    // Force context switch if needed
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

/**
* @brief Task that handles button press events.
*
* @details
* This task blocks indefinitely on a task notification. Each time the ISR
* calls `vTaskNotifyGiveFromISR()`, the notification count is incremented and
* this task is unblocked. The task then consumes exactly one notification with
* `ulTaskNotifyTake(pdTRUE, portMAX_DELAY)` and logs an informational message.
*
* @param[in] pvParameter Unused parameter (conventional FreeRTOS task signature).
*/
void button_task(void *pvParameter) {
    while (1) {
        // Wait indefinitely until ISR notifies
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        ESP_LOGI(TAG, "Button pressed, handled in task!");
    }
}

/**
* @brief Application entry point executed by ESP-IDF after startup.
*
* @details
* - Configures the button GPIO for falling-edge interrupts with internal pull-up.
* - Creates the button task and stores its handle for ISR notifications.
* - Installs the GPIO ISR service and registers the ISR handler for the button.
*
* @note Adjust `BUTTON_GPIO` and interrupt type/pulls to match your hardware.
*/
void app_main() {
    // Configure button pin
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE
    };
    gpio_config(&io_conf);

    // Create task
    xTaskCreate(button_task, "ButtonTask", 2048, NULL, 10,
                &button_task_handle);

    // Install ISR service
    gpio_install_isr_service(0);

    // Attach the ISR handler for the button GPIO
    gpio_isr_handler_add(BUTTON_GPIO, button_isr_handler, NULL);
}
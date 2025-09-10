/**
 * @file main.c
 * @brief Demonstrates FreeRTOS Task Notifications on ESP32 using ESP-IDF.
 *
 * Overview:
 *   This example shows how to signal a task from a GPIO interrupt using a
 *   direct-to-task notification. When the button connected to BUTTON_GPIO
 *   (active-low) is pressed, the ISR notifies a waiting task. The task
 *   blocks on ulTaskNotifyTake() and logs a message each time it is
 *   unblocked by the ISR.
 *
 * Hardware:
 *   - ESP32 board.
 *   - Momentary push-button connected to GPIO0 (BOOT) with the internal
 *     pull-up enabled. A press generates a falling edge interrupt.
 *
 * Key Concepts:
 *   - Direct-to-task notifications as a lightweight alternative to queues
 *     and semaphores for one-to-one signaling.
 *   - Proper ISR-safe APIs (vTaskNotifyGiveFromISR) and conditional
 *     context switch using portYIELD_FROM_ISR().
 *
 * Build & Run:
 *   - Configure and flash with idf.py.
 *   - Open serial monitor; press the button to see log messages.
 *
 * Threading/Timing:
 *   - One high-priority task blocks indefinitely on a notification.
 *   - The ISR posts a single "counting" notification per press.
 *
 * Safety Notes:
 *   - ISRs must only call *_FromISR variants of FreeRTOS APIs.
 *   - Keep ISR work minimal; the actual processing happens in the task.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define BUTTON_GPIO 0
#define TAG "DAY16"

// Task handle for the button task, used to notify it from the ISR.
static TaskHandle_t button_task_handle = NULL;

/**
 * @brief GPIO interrupt service routine for the button.
 *
 * Signals the waiting task using a direct-to-task notification in ISR
 * context. If the notification wakes a higher-priority task, a context
 * switch is requested before exiting the ISR.
 *
 * Args:
 *   arg: (void*) Optional user data passed when the handler is registered.
 *        This example does not use it and expects NULL.
 *
 * Behavior:
 *   - Calls vTaskNotifyGiveFromISR() to increment the task's notification
 *     value (counting semaphore behavior).
 *   - Requests a yield from ISR if a higher-priority task is unblocked.
 *
 * Constraints:
 *   - Must remain in IRAM and use *_FromISR APIs only.
 *   - Must be as short and deterministic as possible.
 */
static void IRAM_ATTR button_isr_handler(void *arg)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // Post a notification to the waiting task (increments its notify count).
    vTaskNotifyGiveFromISR(button_task_handle, &xHigherPriorityTaskWoken);

    // If the unblocked task has higher priority, yield to it immediately.
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

/**
 * @brief Task that blocks awaiting button press notifications.
 *
 * This task waits indefinitely for a direct-to-task notification posted by
 * the GPIO ISR. Each time the button is pressed (falling edge), the ISR
 * gives a notification and this task wakes up, consumes the notification,
 * and logs an informational message.
 *
 * Args:
 *   pvParameter: (void*) Unused in this example; pass NULL.
 *
 * Behavior:
 *   - ulTaskNotifyTake(pdTRUE, portMAX_DELAY) blocks until a notification
 *     is available and then atomically clears the count (pdTRUE).
 *   - Logs upon each button press event.
 */
static void button_task(void *pvParameter)
{
    (void)pvParameter;

    for (;;) {
        // Block until the ISR gives a notification; then clear the count.
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        ESP_LOGI(TAG, "Button pressed! Task unblocked by ISR");
    }
}

/**
 * @brief ESP-IDF application entry point.
 *
 * Configures the button GPIO with a falling-edge interrupt, installs the
 * ISR service, registers the button ISR, and creates the notification
 * receiver task.
 *
 * Initialization Steps:
 *   1) Configure GPIO0 as input with pull-up and NEGEDGE interrupt.
 *   2) Install GPIO ISR service and add the per-pin handler.
 *   3) Create the button task and store its TaskHandle_t for ISR use.
 *
 * Notes:
 *   - The task is created with priority 10 so it can preempt typical
 *     default-priority tasks upon notification.
 *   - The ISR service uses a default (0) flags configuration.
 */
void app_main(void)
{
    // Configure button pin (active-low with internal pull-up).
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE
    };
    gpio_config(&io_conf);

    // Install ISR service and attach ISR to the button pin.
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_GPIO, button_isr_handler, NULL);

    // Create the task that waits for notifications from the ISR.
    xTaskCreate(
        button_task,         // Task function
        "ButtonTask",        // Name for identification
        2048,                // Stack size in bytes
        NULL,                // Parameter (unused)
        10,                  // Priority (higher than idle/default)
        &button_task_handle  // Out: task handle for ISR notifications
    );
}
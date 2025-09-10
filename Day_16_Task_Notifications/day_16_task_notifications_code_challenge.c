/**
 * @file main.c
 * @brief Demonstrates FreeRTOS Task Notifications (ISR + simulated presses) on ESP32.
 *
 * Overview:
 *   This example shows two producers for a single consumer task:
 *     1) A GPIO ISR uses vTaskNotifyGiveFromISR() on a falling edge (real button press).
 *     2) A simulator task uses xTaskNotify(..., eIncrement) to emulate presses in software.
 *   The consumer task blocks on ulTaskNotifyTake(pdTRUE, ...) and counts how many
 *   notifications (press events) arrived since it blocked, accumulating a running total.
 *
 * Key Concepts:
 *   - Direct-to-task notifications as a lightweight counting semaphore.
 *   - Mixing ISR and task-based “give” paths into one notification receiver.
 *   - Using eIncrement with xTaskNotify to model counting behavior.
 *
 * Hardware:
 *   - Button on GPIO0 (BOOT), active-low. Internal pull-up enabled.
 *
 * Build & Run:
 *   - Flash with idf.py, open the monitor, press the button and/or watch the simulator output.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define BUTTON_GPIO 0
#define TAG "DAY16"

// Handle for the button consumer task, used to notify it from the ISR.
static TaskHandle_t button_task_handle = NULL;

/**
 * @brief GPIO interrupt handler: increments the notification count for the consumer task.
 *
 * Args:
 *   arg: (void*) Unused in this example.
 *
 * Behavior:
 *   - Uses vTaskNotifyGiveFromISR() to increment the task’s notification value.
 *   - Yields from ISR if a higher-priority task was unblocked.
 */
static void IRAM_ATTR button_isr_handler(void *arg)
{
    (void)arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // Notify the button task that a button press occurred. 
    // This increments the notification value for the task.
    vTaskNotifyGiveFromISR(button_task_handle, &xHigherPriorityTaskWoken);

    // If the notification caused a higher-priority task to unblock, yield to it immediately.
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

/**
 * @brief Consumer task that blocks on notifications and maintains a running press counter.
 *
 * Args:
 *   pvParameter: (void*) Unused.
 *
 * Behavior:
 *   - ulTaskNotifyTake(pdTRUE, portMAX_DELAY) blocks until >=1 notifications are available.
 *   - The return value (n) is the number of pending notifications consumed in this unblock.
 *   - Accumulates a running total and logs “Button pressed X times (got n this time)”.
 */
static void button_task(void *pvParameter)
{
    (void)pvParameter;
    uint32_t total_presses = 0;

    for (;;) {
        // Consume all pending notifications and clear them (pdTRUE).        
        // Block indefinitely until at least one notification is available.
        // The return value is the number of notifications received since the last call.
        uint32_t n = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // n >= 1 if we were unblocked by either ISR or simulator bursts.
        total_presses += n;
        ESP_LOGI(TAG, "Button pressed %u times (got %u this time)", (unsigned)total_presses, (unsigned)n);
    }
}

/**
 * @brief Simulator task that generates synthetic “presses” using xTaskNotify(..., eIncrement).
 *
 * Args:
 *   pvParameter: (void*) Unused.
 *
 * Behavior:
 *   - Periodically increments the notification value of the consumer task.
 *   - Demonstrates that multiple increments between consumer wake-ups will be coalesced,
 *     allowing ulTaskNotifyTake() to return counts > 1.
 *
 * Timing:
 *   - Every 2000 ms: send a single simulated press.
 *   - Every 5000 ms: send a small burst of 3 simulated presses.
 */
static void simulator_task(void *pvParameter)
{
    (void)pvParameter;

    const TickType_t single_period = pdMS_TO_TICKS(2000);
    const TickType_t burst_period  = pdMS_TO_TICKS(5000);
    TickType_t last_burst = xTaskGetTickCount();

    for (;;) {
        // Single simulated press

        // Notify the button task that a simulated press occurred. 
        // This increments the notification value for the task.
        // We use eIncrement to indicate that we want to increment the notification value by 1
        xTaskNotify(button_task_handle, 0 /*ignored*/, eIncrement);
        vTaskDelay(single_period);

        // Occasional burst to show coalescing behavior (>1 returned by ulTaskNotifyTake)
        TickType_t now = xTaskGetTickCount();
        if ((now - last_burst) >= burst_period) {
            for (int i = 0; i < 3; ++i) {
                xTaskNotify(button_task_handle, 0, eIncrement);
            }
            last_burst = now;
        }
    }
}

/**
 * @brief Application entry point: configures GPIO interrupt, installs ISR, and creates tasks.
 *
 * Steps:
 *   1) Configure GPIO0 as input with pull-up and falling-edge interrupt.
 *   2) Install ISR service and attach @ref button_isr_handler.
 *   3) Create the consumer task (@ref button_task) and the simulator task (@ref simulator_task).
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

    // Create the consumer task that waits for notifications.
    xTaskCreate(
        button_task,
        "ButtonTask",
        2048,
        NULL,
        10,
        &button_task_handle
    );

    // Create the simulator task that generates software “presses”.
    xTaskCreate(
        simulator_task,
        "SimulatorTask",
        2048,
        NULL,
        9,
        NULL
    );
}
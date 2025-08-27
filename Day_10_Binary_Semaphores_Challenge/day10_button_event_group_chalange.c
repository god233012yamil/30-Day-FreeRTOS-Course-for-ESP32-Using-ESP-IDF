/*
    Notes:
    Why two bits (one per task)?
    While event groups can wake multiple tasks on a single bit, you risk a race when 
    using clearOnExit=true (one task might clear the bit before the other consumes it). 
    Assigning distinct bits avoids that ambiguity and gives you clean, deterministic 
    “broadcast” behavior. Each task clears its own bit after it wakes, and the ISR re-sets 
    both bits on the next event.
*/


/**
 * @file day10_button_event_group_chalange.c
 * @brief Demonstrates event group signaling from a GPIO ISR to multiple tasks.
 *
 * Pressing the BOOT button (GPIO0) triggers an ISR that sets multiple bits in
 * an event group. Two tasks wait on separate bits. On each button press, both
 * tasks are released simultaneously, demonstrating broadcast-like behavior.
 * Each task clears its own bit when unblocked.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define BUTTON_GPIO     0
#define TAG             "DAY15_EVT"

#define BIT_TASK_A      (1 << 0)  /**< Event bit for Task A */
#define BIT_TASK_B      (1 << 1)  /**< Event bit for Task B */

// Event group signaled from ISR and waited on by tasks.
static EventGroupHandle_t button_evt_group;

/**
 * @brief GPIO ISR handler: sets bits in the event group.
 *
 * This ISR runs when the BOOT button is pressed (falling edge).
 * It sets both BIT_TASK_A and BIT_TASK_B in the event group,
 * releasing both tasks simultaneously. If a higher-priority task
 * is unblocked, a context switch is requested immediately.
 *
 * @param arg Optional argument (unused).
 */
static void IRAM_ATTR button_isr_handler(void *arg)
{
    (void)arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    xEventGroupSetBitsFromISR(button_evt_group,
                              BIT_TASK_A | BIT_TASK_B,
                              &xHigherPriorityTaskWoken);

    // Request a context switch if needed
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

/**
 * @brief Task A: waits on BIT_TASK_A and logs button press.
 *
 * The task blocks until BIT_TASK_A is set in the event group.
 * With clearOnExit=true, the bit is cleared automatically once
 * consumed. This allows the ISR to re-set it for future presses.
 *
 * @param pvParameter Optional parameter (unused).
 */
static void task_a(void *pvParameter)
{
    (void)pvParameter;
    for (;;) {
        EventBits_t bits = xEventGroupWaitBits(
            button_evt_group,
            BIT_TASK_A,          // bits to wait for
            pdTRUE,              // clearOnExit: clear my bit when I finish waiting
            pdTRUE,              // wait for all bits in mask (just one bit here)
            portMAX_DELAY
        );
        if (bits & BIT_TASK_A) {
            ESP_LOGI(TAG, "[TaskA] observed button press");
        }
    }
}

/**
 * @brief Task B: waits on BIT_TASK_B and logs button press.
 *
 * Same logic as Task A, but uses its own event bit. Both Task A
 * and Task B will be released simultaneously on a single button press.
 *
 * @param pvParameter Optional parameter (unused).
 */
static void task_b(void *pvParameter)
{
    (void)pvParameter;
    for (;;) {
        EventBits_t bits = xEventGroupWaitBits(
            button_evt_group,
            BIT_TASK_B,
            pdTRUE,              // clear my bit on exit
            pdTRUE,
            portMAX_DELAY
        );
        if (bits & BIT_TASK_B) {
            ESP_LOGI(TAG, "[TaskB] observed button press");
        }
    }
}

/**
 * @brief Application entry point.
 *
 * Creates an event group, configures GPIO0 as input with pull-up
 * and falling-edge interrupt, installs the ISR service, and attaches
 * the button ISR handler. Starts two tasks that each wait on their
 * own event bit, allowing both to unblock per button press.
 */
void app_main(void)
{
    // Create the event group
    button_evt_group = xEventGroupCreate();
    if (!button_evt_group) {
        ESP_LOGE(TAG, "Failed to create event group");
        return;
    }

    // Configure GPIO0 as input with pull-up and falling-edge interrupt
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE
    };
    gpio_config(&io_conf);

    // Install ISR service and add the button ISR handler
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_GPIO, button_isr_handler, NULL);

    // Create Task A and Task B
    xTaskCreate(task_a, "TaskA", 2048, NULL, 10, NULL);
    xTaskCreate(task_b, "TaskB", 2048, NULL, 10, NULL);

    ESP_LOGI(TAG, "Press BOOT to test: BOTH TaskA and TaskB should print per press.");
}
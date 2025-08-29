/**
 * @file day11_challenge_isr_producer.c
 * @brief ESP-IDF FreeRTOS challenge: GPIO ISR as producer + counting semaphore.
 *
 * - A GPIO ISR "produces" events (button press / falling edge) using xSemaphoreGiveFromISR().
 * - Two consumer tasks block on the counting semaphore and "consume" events.
 * - If consumers are slower, the counting semaphore accumulates tokens (buffering).
 *
 * Wiring:
 *   - Button between GPIO0 and GND. Internal pull-up is enabled.
 *   - Each falling edge on GPIO0 generates one event.
 *
 * Notes:
 *   - Keep ISR minimal. We only give the semaphore and request a yield if needed.
 *   - Debouncing is handled in task context (simple timing filter shown).
 */

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"

#define TAG                "DAY11_ISR"
#define BTN_GPIO           GPIO_NUM_0       // Use BOOT button on many DevKitC boards
#define SEM_CAPACITY       16               // Max queued events
#define CONSUME_MS         1000             // Simulate slow consumer
#define DEBOUNCE_MS        50               // Simple debounce in task context

static SemaphoreHandle_t event_sem;

/* ------------------------- ISR: Producer ------------------------- */

/**
 * @brief GPIO ISR handler. Gives counting semaphore from ISR.
 *
 * Keep this routine short: give the semaphore and request a yield if a
 * higher-priority task was unblocked.
 */
static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    (void)arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // "Produce" an event by giving the semaphore
    xSemaphoreGiveFromISR(event_sem, &xHigherPriorityTaskWoken);
    
    // Request a context switch if a higher priority task was woken
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

/* ------------------------- Consumers ------------------------- */

/**
 * @brief Consumer task that waits on the counting semaphore and processes events.
 *
 * A very simple debounce is applied in task context: if events arrive within
 * DEBOUNCE_MS of the last handled event, we skip printing/processing (but
 * still "consume" the token—adapt to your needs).
 *
 * @param pvParameters Consumer ID (cast from void*).
 */
static void consumer_task(void *pvParameters)
{
    const int id = (int)(uintptr_t)pvParameters;
    TickType_t last_handled = 0;

    while (1) {
        if (xSemaphoreTake(event_sem, portMAX_DELAY)) {
            TickType_t now = xTaskGetTickCount();
            if ((now - last_handled) >= pdMS_TO_TICKS(DEBOUNCE_MS)) {
                printf("Consumer %d: event consumed (tick=%" PRIu32 ")\n", id, (uint32_t)now);
                last_handled = now;
            } else {
                // Optional: comment this out if you do NOT want to consume bouncy edges
                // printf("Consumer %d: debounced\n", id);
            }

            // Simulate slow work so events can queue up
            vTaskDelay(pdMS_TO_TICKS(CONSUME_MS));
        }
    }
}

/* ------------------------- Setup ------------------------- */

/**
 * @brief Configure GPIO as input with pull-up and falling-edge interrupt.
 */
static esp_err_t configure_button_gpio(void)
{
    // Configure GPIO0 as input with pull-up and falling-edge interrupt
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BTN_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = true,
        .pull_down_en = false,
        .intr_type = GPIO_INTR_NEGEDGE,  // falling edge = press to GND
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    // Install ISR service once in the program
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(BTN_GPIO, gpio_isr_handler, NULL));
    return ESP_OK;
}

/**
 * @brief Application entry point: sets up counting semaphore, GPIO ISR producer, and consumers.
 */
void app_main(void)
{
    // Counting semaphore starts empty; capacity allows burst buffering
    event_sem = xSemaphoreCreateCounting(SEM_CAPACITY, 0);
    if (!event_sem) {
        printf("Failed to create counting semaphore\n");
        return;
    }

    // Configure button GPIO + ISR
    if (configure_button_gpio() != ESP_OK) {
        printf("Failed to configure button GPIO/ISR\n");
        return;
    }

    // Two consumers at the same priority
    xTaskCreate(consumer_task, "Consumer1", 3072, (void *)(uintptr_t)1, 5, NULL);
    xTaskCreate(consumer_task, "Consumer2", 3072, (void *)(uintptr_t)2, 5, NULL);

    printf("Ready. Press the button on GPIO0 to generate events.\n");
}
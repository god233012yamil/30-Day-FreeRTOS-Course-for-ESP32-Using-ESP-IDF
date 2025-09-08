/**
 * @file event_groups_demo.c
 * @brief ESP-IDF demo: Using FreeRTOS Event Groups for system flags and broadcast notifications.
 *
 * This example demonstrates:
 * 1) Waiting for multiple system-readiness bits (Wi-Fi + MQTT) before continuing.
 * 2) Using a GPIO ISR to set an event-bit and broadcast the event to multiple tasks.
 *
 * Build: idf.py build
 * Run:   idf.py -p <PORT> flash monitor
 */

#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_bit_defs.h"   // For BIT0, BIT1, etc.

#define TAG "EVGROUP_DEMO"

// -------------------------------
// Event group and application bits
// -------------------------------
static EventGroupHandle_t app_events;

// Only use lower 24 bits for app flags
#define WIFI_CONNECTED_BIT   BIT0
#define MQTT_CONNECTED_BIT   BIT1
#define BUTTON_PRESSED_BIT   BIT2

// -------------------------------
// GPIO used for the "button"
// -------------------------------
#define BUTTON_GPIO          GPIO_NUM_0   // Boot button on many ESP32 DevKits
#define BUTTON_GPIO_INTR     GPIO_INTR_NEGEDGE

// Forward declarations
static void controller_listener_task(void *arg);
static void button_listener_task_A(void *arg);
static void button_listener_task_B(void *arg);
static void simulate_wifi_mqtt_task(void *arg);
static void IRAM_ATTR button_isr_handler(void *arg);

/**
 * @brief Configure the button GPIO and attach ISR.
 *
 * Sets the pin as input with internal pull-up and enables falling-edge interrupt.
 */
static void button_init_isr(void)
{
    // Configure the button GPIO
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1,             // typical for boot button
        .pull_down_en = 0,
        .intr_type = BUTTON_GPIO_INTR
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(BUTTON_GPIO, button_isr_handler, NULL));
}

/**
 * @brief ISR: set the BUTTON_PRESSED_BIT when the button is pressed.
 *
 * Uses the ISR-safe API to set bits from interrupt context.
 */
static void IRAM_ATTR button_isr_handler(void *arg)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // Set the button event bit
    xEventGroupSetBitsFromISR(app_events, BUTTON_PRESSED_BIT, &xHigherPriorityTaskWoken);

    // If xHigherPriorityTaskWoken was set to true, we should yield.
    if (xHigherPriorityTaskWoken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

/**
 * @brief Simulates subsystem readiness: sets Wi-Fi and MQTT bits after delays.
 *
 * In a real project you would set these from actual event callbacks (e.g., IP event, MQTT event).
 */
static void simulate_wifi_mqtt_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(1500));  // simulate Wi-Fi connect time
    ESP_LOGI(TAG, "Sim: Wi-Fi connected");
    xEventGroupSetBits(app_events, WIFI_CONNECTED_BIT);

    vTaskDelay(pdMS_TO_TICKS(1200));  // simulate MQTT connect after Wi-Fi
    ESP_LOGI(TAG, "Sim: MQTT connected");
    xEventGroupSetBits(app_events, MQTT_CONNECTED_BIT);

    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "Sim: ready bits are set; controller can proceed.");

    // Task can delete itself if no longer needed
    vTaskDelete(NULL);
}

/**
 * @brief Task that waits for ALL system flags (Wi-Fi + MQTT) to be set.
 *
 * Demonstrates waiting for multiple bits using wait-for-all semantics. We clear on exit
 * so this task consumes the “ready” condition once and proceeds.
 */
static void controller_listener_task(void *arg)
{
    ESP_LOGI(TAG, "Controller: waiting for Wi-Fi + MQTT ready...");

    // Wait for both bits to be set (controller cannot proceed until both are ready)
    const EventBits_t wanted = WIFI_CONNECTED_BIT | MQTT_CONNECTED_BIT;

    // Wait indefinitely for the WiFi and MQTT bits to be set
    EventBits_t bits = xEventGroupWaitBits(
        app_events,
        wanted,
        /*clearOnExit=*/pdTRUE,    // consume the "ready" condition once
        /*waitForAllBits=*/pdTRUE, // ALL bits must be set
        /*ticksToWait=*/portMAX_DELAY
    );

    if ((bits & wanted) == wanted) {
        ESP_LOGI(TAG, "Controller: system is READY (Wi-Fi+MQTT). Starting app logic...");
    } else {
        ESP_LOGW(TAG, "Controller: wait returned without all bits (unexpected).");
    }

    // Do app work here...
    for (;;) {
        ESP_LOGI(TAG, "Controller: running...");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

/**
 * @brief Task A that receives broadcasted button events.
 *
 * Uses clearOnExit=false so multiple listeners can observe the same event occurrence.
 * After waking, it explicitly clears the bit to arm the next occurrence (optional pattern).
 */
static void button_listener_task_A(void *arg)
{
    for (;;) {

        // Wait indefinitely for the button event bit to be set
        xEventGroupWaitBits(
            app_events,
            BUTTON_PRESSED_BIT,
            /*clearOnExit=*/pdFALSE,   // keep set to let ALL listeners wake
            /*waitForAllBits=*/pdFALSE,
            portMAX_DELAY
        );

        // Read timestamp for demonstration
        int64_t t_us = esp_timer_get_time();
        ESP_LOGI(TAG, "[A] Button event at %" PRId64 " us", t_us);

        // Optional: clear the bit AFTER all listeners had a chance to run.
        // A simple technique is: the last listener clears, or a dedicated "janitor" clears.
        // For demo, A will clear; if B hasn't run yet, it will still see the bit already set.
        xEventGroupClearBits(app_events, BUTTON_PRESSED_BIT);
    }
}

/**
 * @brief Task B that also receives broadcasted button events.
 *
 * Identical waiting pattern as Task A. In real apps, avoid racing clears between listeners;
 * prefer a dedicated clearer or keep clearOnExit=false and leave the bit as a level/state
 * that is reset by a producer when appropriate.
 */
static void button_listener_task_B(void *arg)
{
    for (;;) {

        // Wait indefinitely for the button event bit to be set
        xEventGroupWaitBits(
            app_events,
            BUTTON_PRESSED_BIT,
            /*clearOnExit=*/pdFALSE,
            /*waitForAllBits=*/pdFALSE,
            portMAX_DELAY
        );

        int64_t t_us = esp_timer_get_time();
        ESP_LOGI(TAG, "[B] Button event at %" PRId64 " us", t_us);

        // Do not clear here to avoid racing with A. In many designs, nobody clears and the
        // producer (ISR) briefly clears then sets to create a new edge, or a timer debounces.
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/**
 * @brief Application entry point.
 *
 * Creates the event group, configures the button ISR, and starts tasks that:
 *  - wait for Wi-Fi+MQTT flags (controller),
 *  - simulate setting those flags,
 *  - listen to button events (two separate tasks, broadcast style).
 */
void app_main(void)
{
    // Create the event group
    app_events = xEventGroupCreate();
    configASSERT(app_events != NULL);

    // Initialize the button GPIO and ISR
    button_init_isr();

    // Create tasks
    xTaskCreate(controller_listener_task, "controller",     4096, NULL, 4, NULL);    
    xTaskCreate(button_listener_task_A,   "btn_listener_A", 3072, NULL, 2, NULL);
    xTaskCreate(button_listener_task_B,   "btn_listener_B", 3072, NULL, 2, NULL);
    xTaskCreate(simulate_wifi_mqtt_task,  "sim_wifi_mqtt",  3072, NULL, 3, NULL);

    ESP_LOGI(TAG, "Press the BOOT button to generate events.");
}
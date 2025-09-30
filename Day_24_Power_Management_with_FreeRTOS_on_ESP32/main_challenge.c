/**
 * @file main.c
 * 
 * @brief ESP32 Power Management — Deep Sleep with Timer + EXT0 (GPIO0) Wake
 *
 * Overview:
 *   Demonstrates entering Deep Sleep for 10 seconds while also enabling
 *   wakeup on an external button press using EXT0 on GPIO0 (active LOW).
 *   On wake, prints the wakeup cause (timer vs GPIO).
 *
 * Key points:
 *   - Use `esp_sleep_enable_timer_wakeup(10s)` so the RTC can wake after 10 s.
 *   - Use `esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0)` to wake on a button
 *     press that pulls GPIO0 to GND.
 *   - Configure an internal pull-up on GPIO0 so it idles HIGH.
 *   - Deep sleep resets the CPU; app_main() runs again after each wakeup.
 *
 * Build/Flash/Monitor:
 *   idf.py set-target esp32
 *   idf.py build flash monitor
 *
 * Hardware:
 *   - ESP32 board with GPIO0 available (boot strap pin on many boards).
 *   - Momentary pushbutton from GPIO0 to GND.
 *
 * Notes:
 *   - EXT0 uses RTC IO; keep wiring short/noisy-free. Internal pull-up is enabled.
 *   - On some boards GPIO0 is a boot strap pin; avoid holding it LOW at reset unless intended.
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"

#define TAG "PM_DEEPSLEEP_EXT0"
#define DEEP_SLEEP_US       (10ULL * 1000000ULL)  // 10 seconds
#define WAKE_PIN            GPIO_NUM_0            // Button to GND; active LOW for EXT0

/**
 * Configure the EXT0 wake source on GPIO0 (active LOW).
 *
 * Enables an internal pull-up so the line idles HIGH and is pulled LOW
 * when the button to GND is pressed. Sets EXT0 wake to trigger when the
 * pin level is 0.
 *
 * Args:
 *   pin (gpio_num_t): The RTC-capable GPIO to use for EXT0 (e.g., GPIO_NUM_0).
 *
 * Returns:
 *   esp_err_t: ESP_OK on success, error code otherwise.
 *
 * Safety:
 *   - EXT0 uses RTC IO; ensure the selected pin is RTC-capable on your SoC.
 *   - Avoid external circuitry that forces the pin LOW at boot unintentionally.
 */
static esp_err_t configure_ext0_wakeup(gpio_num_t pin) {
    // Basic sanity: ensure valid pin
    if (pin == GPIO_NUM_NC) {
        return ESP_ERR_INVALID_ARG;
    }

    // Enable pull-up so the pin stays HIGH when button is not pressed.
    // Use RTC-domain helpers because EXT0 uses RTC IO.
    ESP_ERROR_CHECK(rtc_gpio_pullup_en(pin));
    ESP_ERROR_CHECK(rtc_gpio_pulldown_dis(pin));

    // Also configure as input in the digital domain (safe & explicit).
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    // EXT0 wake on logic level 0 (active LOW)
    return esp_sleep_enable_ext0_wakeup(pin, 0);
}

/**
 * Print the wakeup cause with a friendly message.
 *
 * Reads `esp_sleep_get_wakeup_cause()` and logs whether the system woke
 * from timer or GPIO (EXT0). If neither matches, logs the raw cause enum.
 *
 * Args:
 *   (None)
 *
 * Returns:
 *   (None)
 */
static void print_wakeup_cause(void) {
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    switch (cause) {
        case ESP_SLEEP_WAKEUP_TIMER:
            ESP_LOGI(TAG, "Wakeup cause: RTC TIMER (10 seconds elapsed).");
            break;
        case ESP_SLEEP_WAKEUP_EXT0:
            ESP_LOGI(TAG, "Wakeup cause: EXT0 (GPIO0 went LOW — button press).");
            break;
        case ESP_SLEEP_WAKEUP_UNDEFINED:
            ESP_LOGI(TAG, "Power-on or reset (no deep-sleep wake cause).");
            break;
        default:
            ESP_LOGI(TAG, "Wakeup cause enum: %d", (int)cause);
            break;
    }
}

/**
 * Application entry point.
 *
 * On every boot (including wakes from deep sleep):
 *   1) Prints the wakeup cause.
 *   2) Enables both 10-second timer wake and EXT0 (GPIO0, active LOW) wake.
 *   3) Enters deep sleep.
 *
 * Press the button (GPIO0->GND) to wake immediately, or wait 10 seconds
 * for the timer wakeup. After wake, app_main() runs again and logs the cause.
 *
 * Args:
 *   (None)
 *
 * Returns:
 *   (None)
 */
void app_main(void) {
    // 1) Show why we woke up
    print_wakeup_cause();

    // 2) Configure wake sources: 10s timer + EXT0 on GPIO0 LOW
    esp_err_t err = esp_sleep_enable_timer_wakeup(DEEP_SLEEP_US);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable timer wakeup: %s", esp_err_to_name(err));
    }

    // 3) Configure EXT0 wakeup on the specified pin
    err = configure_ext0_wakeup(WAKE_PIN);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable EXT0 wakeup on GPIO%d: %s", (int)WAKE_PIN, esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "Deep sleep configured: timer=10s, EXT0 on GPIO0 (active LOW).");
    ESP_LOGI(TAG, "Press the button (GPIO0->GND) to wake early, or wait 10 seconds.");

    // Give logs a moment to flush before sleeping (optional but helpful).
    vTaskDelay(pdMS_TO_TICKS(100));

    // 4) Enter deep sleep — CPU and most peripherals power down.
    ESP_LOGI(TAG, "Entering deep sleep now...");
    esp_deep_sleep_start();

    // Not reached
}
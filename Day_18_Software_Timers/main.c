#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "esp_log.h"

#define TAG "DAY18"

// Timer handles
TimerHandle_t one_shot_timer;
TimerHandle_t periodic_timer;

// Callback for one-shot timer
void one_shot_callback(TimerHandle_t xTimer) {
    ESP_LOGI(TAG, "One-shot timer expired!");
}

// Callback for periodic timer
void periodic_callback(TimerHandle_t xTimer) {
    static int count = 0;
    count++;
    ESP_LOGI(TAG, "Periodic timer tick %d", count);

    if (count == 5) {
        ESP_LOGI(TAG, "Stopping periodic timer after 5 ticks");
        xTimerStop(xTimer, 0);
    }
}

// Main application entry point
void app_main() {
    // Create timers with 3s interval (one-shot and periodic)
    one_shot_timer = xTimerCreate("OneShot",
                                  pdMS_TO_TICKS(3000),  // 3s delay
                                  pdFALSE,              // One-shot
                                  NULL,
                                  one_shot_callback);

    // Create periodic timer with 1s interval                       
    periodic_timer = xTimerCreate("Periodic",
                                  pdMS_TO_TICKS(1000),  // 1s interval
                                  pdTRUE,               // Auto-reload
                                  NULL,
                                  periodic_callback);

    // Start timers
    xTimerStart(one_shot_timer, 0);
    xTimerStart(periodic_timer, 0);
}
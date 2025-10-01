/**
 * @file main.c
 * @brief Day 25 – Inter-Task Communication demo on ESP32 (ESP-IDF + FreeRTOS) with Button ISR + Tick Hook Watchdog.
 *
 * @details
 * This example demonstrates several FreeRTOS inter-task communication primitives:
 *
 *  - **Queue** (`sensor_queue`) to pass integer temperature samples from a producer (`sensor_task`)
 *    to a consumer (`processor_task`).
 *  - **Counting Semaphore** (`sample_sem`) as a "new-sample" signal for the processor.
 *  - **Mutex** (`uart_mutex`) to serialize access to stdout/ UART for log prints.
 *  - **Event Group** (`system_event_group`) to signal system state (sensor and processor readiness).
 *  - **Binary Semaphore** (`emergency_sem`) given from a **GPIO ISR** when a button is pressed;
 *    an `emergency_task` takes it and prints an emergency log line (mutex-protected).
 *  - **Direct-to-task notification** used as a lightweight wake-up mechanism for a **watchdog task**.
 *    The notification is posted from `vApplicationTickHook()` every 2000 ms.
 *
 * Flow (new parts):
 *  1) A button (e.g., GPIO0) is configured to trigger an interrupt on falling edge.
 *     The ISR gives `emergency_sem` (FromISR API). `emergency_task` wakes and logs immediately.
 *  2) The tick hook accumulates RTOS ticks and every 2 seconds uses `vTaskNotifyGiveFromISR()`
 *     to wake `watchdog_task`, which performs periodic supervision/logging.
 *
 * @note Requires `CONFIG_FREERTOS_USE_TICK_HOOK=y` in sdkconfig.
 * @note Ensure the chosen button pin supports interrupts and has proper pull-up/down hardware.
 * @copyright
 *  MIT-style usage intended for educational purposes.
 */

#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define TAG "DAY25"

// ---- Event group bits -------------------------------------------------------
#define BIT_SENSOR_READY    (1 << 0)
#define BIT_PROCESSOR_READY (1 << 1)

// ---- GPIO configuration for button -----------------------------------------
#define BUTTON_GPIO         GPIO_NUM_0     // Change if you use a different pin
#define BUTTON_INTR_TYPE    GPIO_INTR_NEGEDGE

// ---- Handles ----------------------------------------------------------------
static QueueHandle_t     sensor_queue;
static SemaphoreHandle_t sample_sem;
static SemaphoreHandle_t uart_mutex;
static EventGroupHandle_t system_event_group;

// New: Binary semaphore for button ISR -> emergency task
static SemaphoreHandle_t emergency_sem;

// New: Watchdog task handle so the tick hook can notify it
static TaskHandle_t watchdog_task_handle = NULL;

// ---- Forward declarations ----------------------------------------------------
void sensor_task(void *pvParameters);
void processor_task(void *pvParameters);
void logger_task(void *pvParameters);
void emergency_task(void *pvParameters);
void watchdog_task(void *pvParameters);
static void configure_button_isr(void);

// ---- Original tasks ----------------------------------------------------------

/**
 * @brief Producer task that simulates a temperature sensor.
 *
 * @param[in] pvParameters Unused (pass NULL).
 *
 * @details
 * Periodically (every 1000 ms) generates a pseudo-random temperature in the
 * range [20, 29] °C, sends it to `sensor_queue`, signals new data via the
 * counting semaphore `sample_sem`, and sets `BIT_SENSOR_READY` in the
 * event group. Runs indefinitely.
 *
 * @pre `sensor_queue`, `sample_sem`, and `system_event_group` must be created.
 */
void sensor_task(void *pvParameters) {
    int temp = 25;
    while (1) {
        temp = 20 + rand() % 10; // fake data
        xQueueSend(sensor_queue, &temp, portMAX_DELAY);
        xSemaphoreGive(sample_sem); // signal new sample
        xEventGroupSetBits(system_event_group, BIT_SENSOR_READY);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/**
 * @brief Consumer/processor task that averages temperature samples.
 *
 * @param[in] pvParameters Unused (pass NULL).
 *
 * @details
 * Waits on `sample_sem` indicating new data is likely available, then
 * attempts a non-blocking read from `sensor_queue`. Accumulates 5 samples,
 * computes the average, logs it, resets its accumulator, and sets
 * `BIT_PROCESSOR_READY` in the event group. Runs indefinitely.
 *
 * @pre `sensor_queue`, `sample_sem`, and `system_event_group` must be created.
 */
void processor_task(void *pvParameters) {
    int value;
    int sum = 0, count = 0;

    while (1) {
        if (xSemaphoreTake(sample_sem, portMAX_DELAY)) {
            if (xQueueReceive(sensor_queue, &value, 0)) {
                sum += value;
                count++;
                if (count == 5) {
                    int avg = sum / count;
                    sum = 0;
                    count = 0;

                    ESP_LOGI(TAG, "Computed avg temp: %d", avg);
                    xEventGroupSetBits(system_event_group, BIT_PROCESSOR_READY);
                }
            }
        }
    }
}

/**
 * @brief Logger task that prints a message once both producer and processor are active.
 *
 * @param[in] pvParameters Unused (pass NULL).
 *
 * @details
 * Waits (atomically) for both `BIT_SENSOR_READY` and `BIT_PROCESSOR_READY`
 * in `system_event_group` (wait-for-all). When both are set, it takes the
 * `uart_mutex` to serialize console output, prints a status message, releases
 * the mutex, and then delays for 2000 ms before checking again.
 * Runs indefinitely.
 *
 * @pre `system_event_group` and `uart_mutex` must be created.
 */
void logger_task(void *pvParameters) {
    while (1) {
        EventBits_t bits = xEventGroupWaitBits(
            system_event_group,
            BIT_SENSOR_READY | BIT_PROCESSOR_READY,
            pdFALSE, // don’t clear bits
            pdTRUE,  // wait for ALL
            portMAX_DELAY
        );

        if ((bits & (BIT_SENSOR_READY | BIT_PROCESSOR_READY)) ==
            (BIT_SENSOR_READY | BIT_PROCESSOR_READY)) {
            if (xSemaphoreTake(uart_mutex, portMAX_DELAY)) {
                printf("Logger: Both Sensor & Processor are active\n");
                xSemaphoreGive(uart_mutex);
            }
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }
}

// ---- New: Emergency path via Button ISR + Binary Semaphore -------------------

/**
 * @brief GPIO interrupt handler for the button.
 *
 * @param[in] arg Unused ISR argument.
 *
 * @details
 * Runs in ISR context when `BUTTON_GPIO` triggers (falling edge). Gives the
 * `emergency_sem` binary semaphore using the FromISR API. If a higher
 * priority task is woken, requests a context switch.
 *
 * @warning ISR functions must reside in IRAM on some targets; `IRAM_ATTR`
 *          attribute helps ensure link placement (if necessary).
 */
static void IRAM_ATTR button_isr_handler(void *arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (emergency_sem) {
        xSemaphoreGiveFromISR(emergency_sem, &xHigherPriorityTaskWoken);
    }
    if (xHigherPriorityTaskWoken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

/**
 * @brief Configure the button GPIO and attach the ISR handler.
 *
 * @details
 * Sets the `BUTTON_GPIO` as input, enables internal pull-up, and configures
 * a falling-edge interrupt. Installs the ISR service and registers
 * `button_isr_handler`.
 *
 * @pre `emergency_sem` must be created before interrupts start firing.
 */
static void configure_button_isr(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = BUTTON_INTR_TYPE
    };
    gpio_config(&io_conf);

    // Install ISR service once
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_GPIO, button_isr_handler, NULL);
}

/**
 * @brief Emergency logger task that reacts to the button ISR.
 *
 * @param[in] pvParameters Unused (pass NULL).
 *
 * @details
 * Blocks on `emergency_sem`. When the semaphore is given by the ISR,
 * takes `uart_mutex` to serialize printing and emits an EMERGENCY log line.
 * This path illustrates fast, deterministic signaling from hardware to task
 * using a binary semaphore.
 *
 * @pre `emergency_sem` and `uart_mutex` must be created, and the GPIO ISR configured.
 */
void emergency_task(void *pvParameters) {
    while (1) {
        if (xSemaphoreTake(emergency_sem, portMAX_DELAY) == pdTRUE) {
            if (xSemaphoreTake(uart_mutex, portMAX_DELAY)) {
                ESP_LOGW(TAG, "EMERGENCY: Button pressed! Taking immediate action.");
                xSemaphoreGive(uart_mutex);
            }
        }
    }
}

// ---- New: Watchdog task + Tick Hook notification every 2 seconds -------------

/**
 * @brief Watchdog task woken by the FreeRTOS tick hook via task notification.
 *
 * @param[in] pvParameters Unused (pass NULL).
 *
 * @details
 * Blocks in `ulTaskNotifyTake(pdTRUE, portMAX_DELAY)`. The notification is posted
 * from `vApplicationTickHook()` every 2000 ms using `vTaskNotifyGiveFromISR()`.
 * Upon wake-up, prints a supervision heartbeat and can be expanded to run
 * health checks (queues depth, missed samples, etc.).
 *
 * @pre `watchdog_task_handle` must be set to this task's handle.
 */
void watchdog_task(void *pvParameters) {
    for (;;) {
        // Wait until the tick hook gives us a notification
        ulTaskNotifyTake(pdTRUE /* clear on exit */, portMAX_DELAY);
        if (xSemaphoreTake(uart_mutex, portMAX_DELAY)) {
            ESP_LOGI(TAG, "Watchdog: periodic supervision OK");
            xSemaphoreGive(uart_mutex);
        }
    }
}

/**
 * @brief FreeRTOS tick hook used to wake the watchdog task every 2 seconds.
 *
 * @details
 * Runs in ISR context at the RTOS tick rate. Accumulates ticks and,
 * every `pdMS_TO_TICKS(2000)`, calls `vTaskNotifyGiveFromISR(watchdog_task_handle, ...)`
 * to wake the watchdog task. Uses `portYIELD_FROM_ISR()` if a higher priority
 * task is woken.
 *
 * @note Requires `CONFIG_FREERTOS_USE_TICK_HOOK=y` in sdkconfig.
 * @warning Keep ISR work minimal. Do not call non-ISR-safe functions here.
 */
void vApplicationTickHook(void) {
    static uint32_t tick_accum = 0;
    const uint32_t EVERY_2S_TICKS = pdMS_TO_TICKS(2000);

    tick_accum++;
    if (tick_accum >= EVERY_2S_TICKS) {
        tick_accum = 0;
        if (watchdog_task_handle != NULL) {
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            vTaskNotifyGiveFromISR(watchdog_task_handle, &xHigherPriorityTaskWoken);
            if (xHigherPriorityTaskWoken == pdTRUE) {
                portYIELD_FROM_ISR();
            }
        }
    }
}

// ---- app_main: create objects and start tasks --------------------------------

/**
 * @brief Application entry point (ESP-IDF).
 *
 * @details
 * Creates the required FreeRTOS objects (queue, counting semaphore, mutex,
 * event group, binary semaphore) and spawns five tasks:
 *  - `sensor_task` (priority 5)
 *  - `processor_task` (priority 6)
 *  - `logger_task` (priority 4)
 *  - `emergency_task` (priority 7)  ← higher priority to react quickly
 *  - `watchdog_task` (priority 5)
 *
 * Configures a GPIO button interrupt to trigger the emergency path.
 * The tick hook (enabled via sdkconfig) will wake the watchdog every 2 seconds.
 *
 * @post All tasks are running and the RTOS scheduler takes over.
 */
void app_main() {
    // Create a queue for sensor data
    sensor_queue = xQueueCreate(10, sizeof(int));

    // Counting semaphore for samples
    sample_sem = xSemaphoreCreateCounting(10, 0);

    // Mutex for UART access
    uart_mutex = xSemaphoreCreateMutex();

    // Event group for system state
    system_event_group = xEventGroupCreate();

    // Binary semaphore for emergency path (button ISR -> emergency task)
    emergency_sem = xSemaphoreCreateBinary();

    // Create tasks
    xTaskCreate(sensor_task,    "SensorTask",    2048, NULL, 5, NULL);
    xTaskCreate(processor_task, "ProcessorTask", 2048, NULL, 6, NULL);
    xTaskCreate(logger_task,    "LoggerTask",    2048, NULL, 4, NULL);
    xTaskCreate(emergency_task, "EmergencyTask", 2048, NULL, 7, NULL);

    // Create watchdog task and capture its handle for tick hook notifications
    xTaskCreate(watchdog_task,  "WatchdogTask",  2048, NULL, 5, &watchdog_task_handle);

    // Configure button GPIO + ISR
    configure_button_isr();

    ESP_LOGI(TAG, "Day 25 system initialized with Button ISR + TickHook Watchdog");
}
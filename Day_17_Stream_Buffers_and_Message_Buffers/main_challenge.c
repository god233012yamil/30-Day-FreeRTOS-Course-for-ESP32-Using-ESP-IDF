/**
 * @file main.c
 *
 * @brief Stream Buffers vs. Message Buffers on ESP32 with an ISR-style sender.
 *
 * Summary:
 *   This extended example adds a third task that simulates an interrupt service routine (ISR)
 *   by calling xStreamBufferSendFromISR() to push short byte bursts into the Stream Buffer.
 *   The producer task still sends data into both buffers, while the consumer task now
 *   counts how many messages it has processed from the Message Buffer.
 *
 * What’s new in this version:
 *   1) ISR simulation task (isr_sim_task): periodically pretends to be an ISR and uses
 *      xStreamBufferSendFromISR() to push "ISR!" bytes. This demonstrates the correct API
 *      to use if data originates in real ISRs (GPIO, UART RX, timers, etc.).
 *      Note: This task is not a true hardware ISR; it’s a controlled environment to show
 *      the API call and potential immediate unblocking of a reader.
 *
 *   2) Consumer message counter: consumer_task tracks the number of discrete messages
 *      received from the Message Buffer and logs the cumulative count.
 *
 *   3) Smaller buffers to force blocking: buffers are intentionally small so you can
 *      observe how writers block (with portMAX_DELAY) until readers drain data.
 *
 * Architecture:
 *   - StreamBufferHandle_t stream_buf: raw/unframed byte stream.
 *   - MessageBufferHandle_t msg_buf: framed messages (each send is one message).
 *   - Tasks:
 *       • producer_task(): sends long-ish byte runs to Stream Buffer (to force blocking)
 *         and sends small messages to Message Buffer.
 *       • isr_sim_task(): "ISR-like" writer using xStreamBufferSendFromISR() to send short bursts.
 *       • consumer_task(): reads both buffers; counts Message Buffer messages.
 *
 * Build & Run:
 *   idf.py set-target esp32
 *   idf.py build flash monitor
 *
 * Tips for experimenting:
 *   - Tweak BUF sizes below to see how often producers block.
 *   - Increase producer payload sizes to stress the Stream Buffer.
 *   - Change task priorities; if the ISR simulation unblocks a higher-priority consumer,
 *     you’ll observe immediate consumption once the scheduler runs.
 *
 * References:
 *   - FreeRTOS Stream Buffers & Message Buffers documentation.
 *   - ESP-IDF FreeRTOS integration.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/stream_buffer.h"
#include "freertos/message_buffer.h"

// ---------------------- Tunables to explore blocking behavior ----------------------
#define STREAM_BUF_SIZE_BYTES   16   // Small on purpose: easy to fill and block writers
#define STREAM_TRIGGER_LEVEL    1    // Unblock readers as soon as >= 1 byte is present
#define MSG_BUF_SIZE_BYTES      32   // Small on purpose: explore blocking on message buffer

#define PRODUCER_DELAY_MS       1500 // Producer pacing
#define ISR_SIM_DELAY_MS        400  // "ISR" pacing (shorter -> more frequent bursts)
#define CONSUMER_POLL_MS        1000 // Consumer waits up to this for each read

// Stream buffer handle for sending and receiving raw byte data between tasks
static StreamBufferHandle_t   stream_buf = NULL;

// Message buffer handle for sending and receiving framed messages between tasks
static MessageBufferHandle_t  msg_buf    = NULL;

/**
 * @brief Producer task: writes into Stream Buffer and Message Buffer, stressing capacity.
 *
 * Args:
 *   pvParameter: Unused (NULL).
 *
 * Behavior:
 *   - Stream Buffer: sends a relatively long byte sequence (longer than buffer capacity)
 *     so you can observe the task block until the consumer drains data.
 *   - Message Buffer: sends two small messages ("Hello", "World").
 *
 * Thread-safety:
 *   - Single writer / single reader pattern used here is supported by FreeRTOS buffers.
 */
static void producer_task(void *pvParameter)
{
    (void)pvParameter;

    // Longer payload to force blocking when buffer fills up.
    const char *stream_data = "abcdefghijklmnopqrstuvwxyz"; // 26 bytes > 16-capacity buffer
    const char *msg1 = "Hello";
    const char *msg2 = "World";

    while (1) {
        // --- Stream Buffer send (may block until consumer reads) ---
        size_t to_send = strlen(stream_data);

        // Send the entire stream_data in one go, which may block if the buffer is full.
        size_t sent = xStreamBufferSend(stream_buf, stream_data, to_send, portMAX_DELAY);
        if (sent == to_send) {
            printf("[Producer] Stream: sent %u bytes (may have blocked if buffer was full)\n", (unsigned)sent);
        } else {
            printf("[Producer] Stream: partial write %u/%u bytes\n", (unsigned)sent, (unsigned)to_send);
        }

        // --- Message Buffer sends (each send is one discrete message) ---
        size_t m1 = xMessageBufferSend(msg_buf, msg1, strlen(msg1) + 1, portMAX_DELAY);
        size_t m2 = xMessageBufferSend(msg_buf, msg2, strlen(msg2) + 1, portMAX_DELAY);
        printf("[Producer] Msg: wrote m1=%u, m2=%u bytes (messages)\n", (unsigned)m1, (unsigned)m2);

        vTaskDelay(pdMS_TO_TICKS(PRODUCER_DELAY_MS));
    }
}

/**
 * @brief "ISR simulation" task: demonstrates using xStreamBufferSendFromISR().
 *
 * Args:
 *   pvParameter: Unused (NULL).
 *
 * Behavior:
 *   - Periodically pretends to be an ISR and pushes a short byte burst ("ISR!") into
 *     the Stream Buffer using xStreamBufferSendFromISR().
 *   - In real projects, you would call this API from an actual interrupt handler
 *     (e.g., UART RX ISR, GPIO ISR) and potentially trigger a context switch via
 *     portYIELD_FROM_ISR() if a higher-priority task was unblocked.
 *
 * Notes:
 *   - This is a pedagogical simulation. We *do not* call portYIELD_FROM_ISR() here because
 *     we are in a normal task context. In a true ISR, you should pass &xHigherPriorityTaskWoken
 *     to xStreamBufferSendFromISR() and then call portYIELD_FROM_ISR() as needed.
 */
static void isr_sim_task(void *pvParameter)
{
    (void)pvParameter;

    const char isr_bytes[] = { 'I','S','R','!'}; // small burst

    while (1) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;

        // Simulate sending from an ISR context by calling the "FromISR" variant of the API.
        size_t sent = xStreamBufferSendFromISR(
            stream_buf,
            isr_bytes,
            sizeof(isr_bytes),
            &xHigherPriorityTaskWoken /* set in real ISR if reader unblocked */);

        // In a real ISR:
        // if (xHigherPriorityTaskWoken) { portYIELD_FROM_ISR(); }

        printf("[ISR-Sim ] Stream: xStreamBufferSendFromISR() sent %u bytes\n", (unsigned)sent);

        vTaskDelay(pdMS_TO_TICKS(ISR_SIM_DELAY_MS));
    }
}

/**
 * @brief Consumer task: reads both buffers and counts processed message-buffer messages.
 *
 * Args:
 *   pvParameter: Unused (NULL).
 *
 * Behavior:
 *   - Stream Buffer: attempts to read up to rx_stream capacity, with timeout.
 *   - Message Buffer: reads one whole message; increments a counter and logs
 *     the cumulative count.
 *
 * Observability:
 *   - With tiny buffers, the producer may block until consumer drains data.
 *   - "ISR" bursts should appear interleaved with producer data in the stream.
 */
static void consumer_task(void *pvParameter)
{
    (void)pvParameter;

    char   rx_stream[32];                 // slightly larger than stream buffer to show partial reads
    char   rx_msg[24];                    // adequate for "Hello"/"World"
    size_t message_count = 0;             // number of messages processed from message buffer

    while (1) {
        // Stream Buffer receive
        size_t got = xStreamBufferReceive(
            stream_buf,
            rx_stream,
            sizeof(rx_stream) - 1,         // leave room for '\0' for printing
            pdMS_TO_TICKS(CONSUMER_POLL_MS));

        if (got > 0) {
            rx_stream[got] = '\0';
            printf("[Consumer] Stream: received %u bytes: \"%s\"\n", (unsigned)got, rx_stream);
        }

        // Message Buffer receive (one full message)
        got = xMessageBufferReceive(
            msg_buf,
            rx_msg,
            sizeof(rx_msg),
            pdMS_TO_TICKS(CONSUMER_POLL_MS));

        if (got > 0) {
            // If sender included '\0', this prints nicely.
            message_count++;
            printf("[Consumer] Msg: \"%s\"  (count=%u)\n", rx_msg, (unsigned)message_count);
        }
    }
}

/**
 * @brief ESP-IDF entry point: creates small buffers and starts the three tasks.
 *
 * Initialization:
 *   - Creates Stream Buffer (very small capacity) with a low trigger level to reveal
 *     blocking on writes when the consumer isn't keeping up.
 *   - Creates Message Buffer (small capacity) so multiple messages can also cause backpressure.
 *   - Starts producer, ISR-sim, and consumer tasks. Equal priorities are fine; you can
 *     experiment with making the consumer higher priority to see faster draining.
 *
 * Errors:
 *   - If any buffer or task creation fails, prints an error and suspends.
 */
void app_main(void)
{
    // Create small buffers to highlight blocking behavior
    stream_buf = xStreamBufferCreate(STREAM_BUF_SIZE_BYTES, STREAM_TRIGGER_LEVEL);
    if (!stream_buf) {
        printf("Error: Failed to create stream buffer\n");
        vTaskSuspend(NULL);
    }

    // Message Buffer creation: small capacity to explore blocking on message sends. 
    msg_buf = xMessageBufferCreate(MSG_BUF_SIZE_BYTES);
    if (!msg_buf) {
        printf("Error: Failed to create message buffer\n");
        vTaskSuspend(NULL);
    }

    // Create tasks: producer, ISR-sim, and consumer. 
    BaseType_t ok1 = xTaskCreate(producer_task, "Producer", 3072, NULL, 5, NULL);
    BaseType_t ok2 = xTaskCreate(consumer_task, "Consumer", 3072, NULL, 5, NULL);
    BaseType_t ok3 = xTaskCreate(isr_sim_task, "ISR_Sim",  2048, NULL, 6, NULL); // give "ISR" a tad more priority

    if (ok1 != pdPASS || ok2 != pdPASS || ok3 != pdPASS) {
        printf("Error: Failed to create one or more tasks\n");
        vTaskSuspend(NULL);
    }
}
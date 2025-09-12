/**
 * @file main.c
 *
 * @brief Stream Buffer and Message Buffer demo on ESP32 (ESP-IDF + FreeRTOS).
 *
 * Summary:
 *   This example creates two tasks that communicate using a Stream Buffer and a
 *   Message Buffer. The producer task periodically writes raw bytes to the
 *   Stream Buffer and discrete, null-terminated strings to the Message Buffer.
 *   The consumer task reads from both buffers and prints the received content.
 *
 * Architecture:
 *   - StreamBufferHandle_t stream_buf: carries raw byte sequences (unframed).
 *   - MessageBufferHandle_t msg_buf: carries framed messages (each send is one message).
 *   - Tasks:
 *       • producer_task(): sends "ABCDEF" to the stream buffer and two messages
 *         ("Hello", "World") to the message buffer every 2 seconds.
 *       • consumer_task(): non-blocking polling (with 1s timeout) of both buffers,
 *         printing any received data/messages.
 *
 * Why two buffer types?
 *   - Stream Buffer: best when you have a continuous byte stream (UART samples,
 *     audio chunks, sensor byte bursts) without inherent boundaries.
 *   - Message Buffer: best when you want to preserve message boundaries
 *     (commands, JSON blobs, lines of text).
 *
 * Build & Run (ESP-IDF):
 *   idf.py set-target esp32     // or esp32s3 / esp32c6, etc.
 *   idf.py build flash monitor
 *
 * Notes:
 *   - For brevity this demo uses printf(). You can switch to ESP_LOG* if preferred.
 *   - Buffer sizes are small for the example; adjust for production workloads.
 *   - Timeouts use pdMS_TO_TICKS() so behavior scales with configTICK_RATE_HZ.
 *
 * References:
 *   - FreeRTOS Stream Buffers API
 *   - FreeRTOS Message Buffers API
 *   - ESP-IDF FreeRTOS integration
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/stream_buffer.h"
#include "freertos/message_buffer.h"

#define TAG "DAY17"

// Stream buffer handle for sending and receiving raw byte data between tasks
static StreamBufferHandle_t stream_buf = NULL;

// Message buffer handle for sending and receiving framed messages between tasks
static MessageBufferHandle_t msg_buf = NULL;

/**
 * @brief Producer task that writes to both a Stream Buffer and a Message Buffer.
 *
 * Args:
 *   pvParameter: Unused in this demo (pass NULL).
 *
 * Behavior:
 *   - Writes the 6 raw bytes "ABCDEF" to the stream buffer.
 *   - Sends two null-terminated messages ("Hello", then "World") to the message buffer.
 *   - Repeats the sequence every 2000 ms.
 *
 * Guarantees:
 *   - Uses portMAX_DELAY to wait for available space, so no data is dropped unless
 *     the scheduler is stopped or the buffer is deleted.
 *
 * Thread-safety:
 *   - Stream/Message Buffer APIs are safe to call from a single writer and single
 *     reader task pattern used here.
 */
static void producer_task(void *pvParameter) {
    const char *stream_data = "ABCDEF";
    const char *msg1 = "Hello";
    const char *msg2 = "World";

    (void)pvParameter;

    while (1) {
        /* Send raw bytes to the stream buffer. */
        size_t sent_stream = xStreamBufferSend(stream_buf, stream_data, 6, portMAX_DELAY);
        if (sent_stream == 6) {
            printf("Producer: Sent 6 bytes to stream buffer\n");
        } else {
            printf("Producer: Warning — partial stream write (%u bytes)\n", (unsigned)sent_stream);
        }

        /* Send messages to the message buffer (include '\0' to keep them C-strings). */
        size_t sent_msg1 = xMessageBufferSend(msg_buf, msg1, strlen(msg1) + 1, portMAX_DELAY);
        size_t sent_msg2 = xMessageBufferSend(msg_buf, msg2, strlen(msg2) + 1, portMAX_DELAY);

        if (sent_msg1 == strlen(msg1) + 1 && sent_msg2 == strlen(msg2) + 1) {
            printf("Producer: Sent two messages to message buffer\n");
        } else {
            printf("Producer: Warning — partial message write(s) (m1=%u, m2=%u)\n",
                   (unsigned)sent_msg1, (unsigned)sent_msg2);
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

/**
 * @brief Consumer task that reads from Stream Buffer and Message Buffer and prints output.
 *
 * Args:
 *   pvParameter: Unused in this demo (pass NULL).
 *
 * Behavior:
 *   - Attempts to read up to sizeof(rx_stream) bytes from the stream buffer,
 *     waiting up to 1000 ms. Prints any received bytes as a C-string (null-terminates).
 *   - Attempts to receive one complete message into rx_msg from the message buffer,
 *     waiting up to 1000 ms. Prints the message if one is received.
 *   - Loops indefinitely.
 *
 * Buffering considerations:
 *   - Stream buffer reads may return fewer bytes than requested depending on what is
 *     currently available. The demo null-terminates to print safely as text.
 *   - Message buffer ensures each xMessageBufferSend() is received by exactly one
 *     xMessageBufferReceive(), preserving boundaries.
 */
static void consumer_task(void *pvParameter) {
    (void)pvParameter;

    char rx_stream[20];
    char rx_msg[20];

    while (1) {
        /* Receive from stream buffer (up to rx_stream capacity). */
        size_t received = xStreamBufferReceive(
            stream_buf, rx_stream, sizeof(rx_stream) - 1 /* leave room for '\0' */,
            pdMS_TO_TICKS(1000));

        if (received > 0) {
            rx_stream[received] = '\0';
            printf("Consumer: Stream buffer received: %s\n", rx_stream);
        }

        /* Receive one message from the message buffer. */
        received = xMessageBufferReceive(
            msg_buf, rx_msg, sizeof(rx_msg), pdMS_TO_TICKS(1000));

        if (received > 0) {
            /* If the sender included '\0', this prints as a C-string. */
            printf("Consumer: Message buffer received: %s\n", rx_msg);
        }
    }
}

/**
 * @brief ESP-IDF entry point: initializes buffers and starts producer/consumer tasks.
 *
 * Initialization:
 *   - Creates a Stream Buffer with a 50-byte storage and a trigger level of 1 byte
 *     (task unblocks when at least 1 byte is available to read).
 *   - Creates a Message Buffer with a 100-byte storage region.
 *   - Spawns the producer and consumer tasks at equal priority (5).
 *
 * Error handling:
 *   - If buffer creation fails, prints an error and suspends the scheduler to stop execution.
 *
 * Returns:
 *   void (never returns).
 */
void app_main(void) {
    /* Create a stream buffer (50 bytes total storage, trigger level = 1 byte). */
    stream_buf = xStreamBufferCreate(50, 1);
    if (stream_buf == NULL) {
        printf("Error: Failed to create stream buffer\n");
        vTaskSuspend(NULL);
    }

    /* Create a message buffer (100 bytes total storage). */
    msg_buf = xMessageBufferCreate(100);
    if (msg_buf == NULL) {
        printf("Error: Failed to create message buffer\n");
        vTaskSuspend(NULL);
    }

    /* Create the producer and consumer tasks. */
    BaseType_t ok1 = xTaskCreate(producer_task, "Producer", 2048, NULL, 5, NULL);
    BaseType_t ok2 = xTaskCreate(consumer_task, "Consumer", 2048, NULL, 5, NULL);

    if (ok1 != pdPASS || ok2 != pdPASS) {
        printf("Error: Failed to create task(s)\n");
        vTaskSuspend(NULL);
    }
}
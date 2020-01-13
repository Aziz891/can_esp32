/* CAN Network Listen Only Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

/*
 * The following example demonstrates a Listen Only node in a CAN network. The
 * Listen Only node will not take part in any CAN bus activity (no acknowledgments
 * and no error frames). This example will execute multiple iterations, with each
 * iteration the Listen Only node will do the following:
 * 1) Listen for ping and ping response
 * 2) Listen for start command
 * 3) Listen for data messages
 * 4) Listen for stop and stop response
 */
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/can.h"
#include <esp_wifi.h>
#include <esp_event_loop.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include "isotp/isotp_types.h"
#include "isotp/receive.h"
#include "isotp/send.h"
#include <esp_http_server.h>

/* --------------------- Definitions and static variables ------------------ */
//Example Configuration
#define NO_OF_ITERS 3
#define RX_TASK_PRIO 9
#define TX_GPIO_NUM 21
#define RX_GPIO_NUM 22
#define EXAMPLE_TAG "CAN Listen Only"

#define ID_MASTER_STOP_CMD 0x0A0
#define ID_MASTER_START_CMD 0x0A1
#define ID_MASTER_PING 0x0A2
#define ID_SLAVE_STOP_RESP 0x0B0
#define ID_SLAVE_DATA 0x0B1
#define ID_SLAVE_PING_RESP 0x0B2
#define QUEUE_SIZE_CAN 20
#define QUEUE_SIZE_SCALE 20
#define QUEUE_SIZE_RX 20
xQueueHandle can_queue;
xQueueHandle can_tp_send_queue;
xQueueHandle can_tp_receive_queue;
xQueueHandle can_queue;
typedef struct
{
    int x;
    can_message_t message;
} can_with_id;

static const can_filter_config_t f_config = {.acceptance_code = 0x7E8 << 21, .acceptance_mask = 0xFFFFFF, .single_filter = true}; //CAN_FILTER_CONFIG_ACCEPT_ALL();//{.acceptance_code = 0x7E8, .acceptance_mask = 0xFFFFFFFF, .single_filter = true};
static const can_timing_config_t t_config = CAN_TIMING_CONFIG_500KBITS();
//Set TX queue length to 0 due to listen only mode
static const can_general_config_t g_config = {
    .mode = CAN_MODE_NORMAL,
    .tx_io = 21,
    .rx_io = 22,
    .clkout_io = CAN_IO_UNUSED,
    .bus_off_io = CAN_IO_UNUSED,
    .tx_queue_len = 5,
    .rx_queue_len = 500,
    .alerts_enabled = 0x0400,
    .clkout_divider = 0,
}; // CAN_GENERAL_CONFIG_DEFAULT(TX_GPIO_NUM, RX_GPIO_NUM, CAN_MODE_NORMAL);

static SemaphoreHandle_t rx_sem;

/* --------------------------- Tasks and Functions -------------------------- */

// #define EXAMPLE_WIFI_SSID "Abdulaziz_2.4G"
// #define EXAMPLE_WIFI_PASS "0504153443"
#define EXAMPLE_WIFI_SSID "ESP32"
#define EXAMPLE_WIFI_PASS "0504153443"

static const char *TAG = "APP";

/* An HTTP GET handler */
esp_err_t hello_get_handler(httpd_req_t *req)
{
    xSemaphoreTake(rx_sem, portMAX_DELAY);
    char *buf;
    size_t buf_len;

    /* Get header value string length and allocate memory for length + 1,
     * extra byte for null termination */
    buf_len = httpd_req_get_hdr_value_len(req, "Host") + 1;
    if (buf_len > 1)
    {
        buf = malloc(buf_len);
        /* Copy null terminated value string into buffer */
        if (httpd_req_get_hdr_value_str(req, "Host", buf, buf_len) == ESP_OK)
        {
            // ESP_LOGI(TAG, "Found header => Host: %s", buf);
        }
        free(buf);
    }

    buf_len = httpd_req_get_hdr_value_len(req, "Test-Header-2") + 1;
    if (buf_len > 1)
    {
        buf = malloc(buf_len);
        if (httpd_req_get_hdr_value_str(req, "Test-Header-2", buf, buf_len) == ESP_OK)
        {
            ESP_LOGI(TAG, "Found header => Test-Header-2: %s", buf);
        }
        free(buf);
    }

    buf_len = httpd_req_get_hdr_value_len(req, "Test-Header-1") + 1;
    if (buf_len > 1)
    {
        buf = malloc(buf_len);
        if (httpd_req_get_hdr_value_str(req, "Test-Header-1", buf, buf_len) == ESP_OK)
        {
            ESP_LOGI(TAG, "Found header => Test-Header-1: %s", buf);
        }
        free(buf);
    }

    /* Read URL query string length and allocate memory for length + 1,
     * extra byte for null termination */
    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1)
    {
        buf = malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK)
        {
            ESP_LOGI(TAG, "Found URL query => %s", buf);
            char param[32];
            /* Get value of expected key from query string */
            if (httpd_query_key_value(buf, "query1", param, sizeof(param)) == ESP_OK)
            {
                ESP_LOGI(TAG, "Found URL query parameter => query1=%s", param);
            }
            if (httpd_query_key_value(buf, "query3", param, sizeof(param)) == ESP_OK)
            {
                ESP_LOGI(TAG, "Found URL query parameter => query3=%s", param);
            }
            if (httpd_query_key_value(buf, "query2", param, sizeof(param)) == ESP_OK)
            {
                ESP_LOGI(TAG, "Found URL query parameter => query2=%s", param);
            }
        }
        free(buf);
    }

    /* Set some custom headers */
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Custom-Header-2", "Custom-Value-2");

    /* Send response with custom headers and body set as the
     * string passed in user context*/
    char *string_final;
    string_final = malloc(QUEUE_SIZE_SCALE * 300);
    char str[30]; // temporary string holder

    string_final[0] = '\0'; // clean up the string_final
    sprintf(str, "{ \"messages\" : [ \n");
    strcat(string_final, str);
    can_with_id message[QUEUE_SIZE_SCALE];
    for (size_t i = 0; i < QUEUE_SIZE_SCALE; i++)
    {
        if (xQueueReceive(can_queue, &message[i], 0) != 0)
        {
            if (i != 0)
            {
                sprintf(str, ",");
                strcat(string_final, str);
            }
            // sprintf( str, "{ \"#\": %d", message[i].x );        strcat( string_final, str);
            sprintf(str, "{\"ID\": %d", message[i].message.identifier);
            strcat(string_final, str);
            sprintf(str, ",\"length\": %u", message[i].message.data_length_code);
            strcat(string_final, str);
            for (uint8_t j = 0; j < message[i].message.data_length_code; j++)
            {
                sprintf(str, ",\"data%u\" : %u", j, message[i].message.data[j]);
                strcat(string_final, str);
            }
            sprintf(str, "}\n");
            strcat(string_final, str);
        }
    }

    sprintf(str, "]}");
    strcat(string_final, str);

    const char *resp_str = (const char *)string_final;
    // ESP_LOGI(TAG, "begin sending response");

    httpd_resp_send(req, resp_str, strlen(resp_str));
    // ESP_LOGI(TAG, "finished sending response");
    free(string_final);

    /* After sending the HTTP response the old HTTP request
     * headers are lost. Check if HTTP request headers can be read now. */
    if (httpd_req_get_hdr_value_len(req, "Host") == 0)
    {
        // ESP_LOGI(TAG, "Request headers lost");
    }
    xSemaphoreGive(rx_sem);
    return ESP_OK;
}

httpd_uri_t hello = {
    .uri = "/hello",
    .method = HTTP_GET,
    .handler = hello_get_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx = "Hello World!"};

/* This handler allows the custom error handling functionality to be
 * tested from client side. For that, when a PUT request 0 is sent to
 * URI /ctrl, the /hello and /echo URIs are unregistered and following
 * custom error handler http_404_error_handler() is registered.
 * Afterwards, when /hello or /echo is requested, this custom error
 * handler is invoked which, after sending an error message to client,
 * either closes the underlying socket (when requested URI is /echo)
 * or keeps it open (when requested URI is /hello). This allows the
 * client to infer if the custom error handler is functioning as expected
 * by observing the socket state.
 */
esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    if (strcmp("/hello", req->uri) == 0)
    {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "/hello URI is not available");
        /* Return ESP_OK to keep underlying socket open */
        return ESP_OK;
    }
    else if (strcmp("/echo", req->uri) == 0)
    {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "/echo URI is not available");
        /* Return ESP_FAIL to close underlying socket */
        return ESP_FAIL;
    }
    /* For any other URI send 404 and close socket */
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Some 404 error message");
    return ESP_FAIL;
}

/* An HTTP PUT handler. This demonstrates realtime
 * registration and deregistration of URI handlers
 */

httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = {.task_priority = tskIDLE_PRIORITY + 5, .stack_size = 1024 * QUEUE_SIZE_SCALE, .server_port = 80, .ctrl_port = 32768, .max_open_sockets = 7, .max_uri_handlers = 8, .max_resp_headers = 8, .backlog_conn = 5, .lru_purge_enable = false, .recv_wait_timeout = 5, .send_wait_timeout = 5, .global_user_ctx = NULL, .global_user_ctx_free_fn = NULL, .global_transport_ctx = NULL, .global_transport_ctx_free_fn = NULL, .open_fn = NULL, .close_fn = NULL, .uri_match_fn = NULL};

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK)
    {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &hello);

        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

void stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    httpd_stop(server);
}

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    httpd_handle_t *server = (httpd_handle_t *)ctx;

    switch (event->event_id)
    {
    case SYSTEM_EVENT_STA_START:
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_START");
        ESP_ERROR_CHECK(esp_wifi_connect());
        break;
    case SYSTEM_EVENT_AP_STACONNECTED:
        ESP_LOGI(TAG, "STA CONNECTED, STARTING WEB SERVER");
        ESP_LOGI(TAG, "Got IP: '%s'",
                 ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));

        /* Start the web server */
        if (*server == NULL)
        {
            *server = start_webserver();
        }
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_DISCONNECTED");
        ESP_ERROR_CHECK(esp_wifi_connect());

        /* Stop the web server */
        if (*server)
        {
            stop_webserver(*server);
            *server = NULL;
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

static void initialise_wifi(void *arg)
{
    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, arg));
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = EXAMPLE_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_WIFI_SSID),
            .password = EXAMPLE_WIFI_PASS,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK

        },
    };
    ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void can_receive_task(void *arg)
{

    can_with_id message_struct[QUEUE_SIZE_CAN];
    int count = 0;
    esp_err_t test;

    while (1)
    {
        xSemaphoreTake(rx_sem, portMAX_DELAY);
        uint32_t alerts;
        can_read_alerts(&alerts, 0);
        //  ESP_LOGI(TAG,"alert----- %d", alerts);

        for (size_t i = 0; i < QUEUE_SIZE_CAN; i++)
        {

            test = can_receive(&message_struct[i].message, 0);
            //  ESP_LOGI(TAG, "receive %d ", test);

            //Process received message
            if (test == ESP_OK)
            {
                if (message_struct[i].message.data[1] == 0x43)
                {

                    xQueueSend(can_tp_receive_queue, &message_struct[i].message, 0);
                }
                xQueueSend(can_queue, &message_struct[i], 0);
            }
        }

        xSemaphoreGive(rx_sem);
        vTaskDelay(1);
    }
}

static void can_send_task(void *arg)
{

    struct pid
    {
        uint8_t service;
        uint8_t code;

        /* data */
    };
    const struct pid pid_codes[4] = {{.service = 3, .code = 12}, {.service = 3, .code = 13}, {.service = 3, .code = 5}, {.service = 3, .code = 31}};

    can_message_t tx_message;
    tx_message.identifier = 0x7DF;
    tx_message.data[0] = 2;
    tx_message.data[1] = 1;
    tx_message.data[2] = 12;
    tx_message.data_length_code = 8;
    esp_err_t test;
    uint8_t count = 0;

    while (1)
    {
        tx_message.data[1] = pid_codes[count].service;
        tx_message.data[2] = pid_codes[count].code;
        test = can_transmit(&tx_message, 0);
        //   ESP_LOGI(TAG, "sent OBD query %d ", test);
        count++;
        if (count == 4)
            count = 0;
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

bool send_can_tp(const uint32_t arbitration_id, const uint8_t *data, const uint8_t size)
{

    can_message_t tx_message;
    tx_message.identifier = arbitration_id;
    memcpy(tx_message.data, data, size);
    tx_message.data_length_code = size;
    esp_err_t test;
    test = can_transmit(&tx_message, 0);
    if (test == ESP_OK)
    {
    ESP_LOGI("debug --ISO-TP---", "sent success");
        return true;
    }
    ESP_LOGI("debug --ISO-TP---", "sent fail");
    return false;
}

void debug(const char *format, ...)
{
    ESP_LOGI("debug --ISO-TP---", "%s", format);

    
}
static void can_send_task_tp(void *arg)
{
    uint8_t payload_tp[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    can_message_t received_message;

    struct pid
   {
       uint8_t service;
       uint8_t code;

       /* data */
   };
   const struct pid pid_codes[4] = {{.service = 1, .code = 5}, {.service = 1, .code = 12}, {.service = 1, .code = 13}, {.service = 1, .code = 16} };
    

 

    while (1)
    {
        ESP_LOGI("ISO-TP", "--------- checking queue");
        
    if (xQueueReceive(can_tp_receive_queue, &received_message, pdMS_TO_TICKS(200)) != 0){
        IsoTpReceiveHandle handle = isotp_receive(&shims, 0x7E8, NULL);
        ESP_LOGI("ISO-TP", "received a message %u %u", received_message.identifier, received_message.data[2]);
        
        // handle = isotp_send(&shims, 0x100, payload_tp, 7, NULL);
      
            // if (!handle.success)
            // {
            //     // something happened and it already failed - possibly we aren't able to
            //     // send CAN messages
            //     ESP_LOGI("ISO-TP", "deleting task");
            //     vTaskDelete(NULL);
            // }
  
        
     
            while (true)
            {
                // Continue to read from CAN, passing off each message to the handle
                // this will return true when the message is completely sent (which
                // may take more than one call if it was multi frame and we're waiting
                // on flow control responses from the receiver)

            
                
                // bool complete = isotp_continue_send(&shims, &handle, 0x100, received_message.data,
                //                 received_message.data_length_code);
                IsoTpMessage message = isotp_continue_receive(&shims, &handle, received_message.identifier, &received_message.data[2],
                     received_message.data[0] -2);

                if (message.completed && handle.completed)
                {
                    if (handle.success)
                    {
                        // All frames of the message have now been sent, following
                        // whatever flow control feedback it got from the receiver
                        ESP_LOGI("ISO-TP", "success");
                        break;
                    }
                    else
                    {
                        // the message was unable to be sent and we bailed - fatal
                        // error!
                        ESP_LOGI("ISO-TP", "error");
                        break;
                    }
                }
                    if (xQueueReceive(can_tp_receive_queue, &received_message, pdMS_TO_TICKS(200)) == 0)
                        {ESP_LOGI("ISO-TP", "timedout waiting for continuation frame");
                        break;
                        }
                ESP_LOGI("ISO-TP", "continue send");

            }
        
    } else {
        ESP_LOGI("ISO-TP", "--------- queue empty");
    }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main()
{

    rx_sem = xSemaphoreCreateBinary();
    can_queue = xQueueCreate(QUEUE_SIZE_CAN, sizeof(can_with_id));
    can_tp_send_queue = xQueueCreate(QUEUE_SIZE_CAN, sizeof(can_message_t));
    can_tp_receive_queue = xQueueCreate(QUEUE_SIZE_CAN, sizeof(can_message_t));
    xTaskCreatePinnedToCore(can_receive_task, "CAN_rx", QUEUE_SIZE_CAN * 400, NULL, 2, NULL, tskNO_AFFINITY);
    xTaskCreatePinnedToCore(can_send_task, "CAN_tx", 4096, NULL, RX_TASK_PRIO, NULL, tskNO_AFFINITY);
    xTaskCreatePinnedToCore(can_send_task_tp, "CAN_tx_tp", 10 * 4096, NULL, RX_TASK_PRIO, NULL, tskNO_AFFINITY);

    //Install and start CAN driver
    ESP_ERROR_CHECK(can_driver_install(&g_config, &t_config, &f_config));
    ESP_LOGI(EXAMPLE_TAG, "Driver installed");
    ESP_ERROR_CHECK(can_start());
    xSemaphoreGive(rx_sem);
    ESP_LOGI(EXAMPLE_TAG, "Driver started");
    static httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(nvs_flash_init());
    initialise_wifi(&server);
}

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
#include "esp_websocket_client.h"

#include <esp_http_server.h>

/* --------------------- Definitions and static variables ------------------ */
//Example Configuration
#define NO_OF_ITERS                     3
#define RX_TASK_PRIO                    9
#define TX_GPIO_NUM                     21
#define RX_GPIO_NUM                     22
#define EXAMPLE_TAG                     "CAN Listen Only"

#define ID_MASTER_STOP_CMD              0x0A0
#define ID_MASTER_START_CMD             0x0A1
#define ID_MASTER_PING                  0x0A2
#define ID_SLAVE_STOP_RESP              0x0B0
#define ID_SLAVE_DATA                   0x0B1
#define ID_SLAVE_PING_RESP              0x0B2
#define QUEUE_SIZE_CAN              50
#define QUEUE_SIZE_SCALE              50
#define QUEUE_SIZE_RX              50
xQueueHandle can_queue;
typedef  can_message_t can_with_id;

static const can_filter_config_t f_config =  {.acceptance_code = 0x7E8<<3, .acceptance_mask = 0x7, .single_filter = true}; // CAN_FILTER_CONFIG_ACCEPT_ALL();//
static const can_timing_config_t t_config = CAN_TIMING_CONFIG_500KBITS();;
//Set TX queue length to 0 due to listen only mode
static const can_general_config_t g_config =  {.mode = CAN_MODE_NORMAL, .tx_io = 21, .rx_io = 22, .clkout_io = CAN_IO_UNUSED, .bus_off_io = CAN_IO_UNUSED, .tx_queue_len = 5, .rx_queue_len =   500, .alerts_enabled =  0x0400 , .clkout_divider = 0, };   // CAN_GENERAL_CONFIG_DEFAULT(TX_GPIO_NUM, RX_GPIO_NUM, CAN_MODE_NORMAL);

static SemaphoreHandle_t rx_sem;

/* --------------------------- Tasks and Functions -------------------------- */

#define EXAMPLE_WIFI_SSID "Abdulaziz_2.4G"
#define EXAMPLE_WIFI_PASS "0504153443"

static const char *TAG="APP";
static const char *WEBSOCKET_ECHO_ENDPOINT =    "ws://192.168.100.111:8765";



/* An HTTP GET handler */
const char* format_can_to_string(xQueueHandle can_queue)
{

char string_final[QUEUE_SIZE_SCALE *100];


char str[30];     // temporary string holder

string_final[0] = '\0';      // clean up the string_final
strcat( string_final, "{ \"messages\" : [ \n");
     can_with_id message[QUEUE_SIZE_SCALE];
    for (size_t i = 0; i < QUEUE_SIZE_SCALE; i++)
    {
        
        if(xQueueReceive(can_queue,&message[i], 0 ) != 0){
     
// sprintf( str, "{ \"#\": %d", message[i].x );        strcat( string_final, str);
sprintf( str, "{\"ID\": %d", message[i].identifier );        strcat( string_final, str);
sprintf( str, ",\"length\": %u", message[i].data_length_code );        strcat( string_final, str);
for (uint8_t j = 0; j < message[i].data_length_code; j++)
{
sprintf( str, ",\"data%u\" : %u", j, message[i].data[j]);   strcat( string_final, str);
    
}
strcat( string_final, "}\n");


if (i != (QUEUE_SIZE_SCALE-1)){
  strcat( string_final, ",");
}
        }
        else 
        break;

     
        /* code */
    }
    // ESP_LOGI(TAG, "finished reading");
     
    // const char* resp_str = (const char*) req->user_ctx;



sprintf( str, "]} ");            strcat( string_final, str);



    
    const char* resp_str = (const char*) string_final;
    
    return resp_str;
}

const char* format_can_to_string_itoa(xQueueHandle can_queue)
{

char string_final[QUEUE_SIZE_SCALE *100];
size_t length = 0;




char str[30];     // temporary string holder

string_final[0] = '\0';      // clean up the string_final
length += sprintf(string_final + length,"%s",  "{ \"messages\" : [ \n");
     can_with_id message[QUEUE_SIZE_SCALE];
    for (size_t i = 0; i < QUEUE_SIZE_SCALE; i++)
    {
        
        if(xQueueReceive(can_queue,&message[i], 0 ) != errQUEUE_EMPTY){
     
// sprintf( str, "{ \"#\": %d", message[i].x );        strcat( string_final, str);
sprintf( str, "{\"ID\": %d", message[i].identifier );        length += sprintf(string_final + length,"%s", str);
sprintf( str, ",\"length\": %u", message[i].data_length_code );        length += sprintf(string_final + length,"%s", str);
for (uint8_t j = 0; j < message[i].data_length_code; j++)
{
sprintf( str, ",\"data%u\" : %u", j, message[i].data[j]);   length += sprintf(string_final + length,"%s", str);
    
}

length += sprintf(string_final + length,"%s", "}\n");


if (i != (QUEUE_SIZE_SCALE-1)){
  length += sprintf(string_final + length,"%s", ",");
}
        }
        else{

    ESP_LOGI("websocket", "queue empty");
        break;

        } 

     
        /* code */
    }
    // ESP_LOGI(TAG, "finished reading");
     
    // const char* resp_str = (const char*) req->user_ctx;



sprintf( str, "]} ");            length += sprintf(string_final + length,"%s", str);



    
    const char* resp_str = (const char*) string_final;
    
    return resp_str;
}

static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    // esp_websocket_client_handle_t client = (esp_websocket_client_handle_t)handler_args;
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "WEBSOCKET_EVENT_CONNECTED");


            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "WEBSOCKET_EVENT_DISCONNECTED");
            break;

        // case WEBSOCKET_EVENT_DATA:
        //     ESP_LOGI(TAG, "WEBSOCKET_EVENT_DATA");
        //     ESP_LOGI(TAG, "Received opcode=%d", data->op_code);
        //     ESP_LOGW(TAG, "Received=%.*s\r\n", data->data_len, (char*)data->data_ptr);
        //     break;
        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGI(TAG, "WEBSOCKET_EVENT_ERROR");
            break;

    }
}
static void websocket_app_start(void *arg)
{
     xSemaphoreTake(rx_sem, portMAX_DELAY);
    ESP_LOGI(TAG, "Connectiong to %s...", WEBSOCKET_ECHO_ENDPOINT);

    const esp_websocket_client_config_t websocket_cfg = {
        .uri = WEBSOCKET_ECHO_ENDPOINT, // or wss://echo.websocket.org for websocket secure
    };

    esp_websocket_client_handle_t client = esp_websocket_client_init(&websocket_cfg);
    ESP_LOGI(TAG, "1");
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)client);
    ESP_LOGI(TAG, "2");

    esp_websocket_client_start(client);
    ESP_LOGI(TAG, "3");
    xSemaphoreGive(rx_sem);
    vTaskDelay(50);
    BaseType_t stack;
    
    while (1) {
     xSemaphoreTake(rx_sem, portMAX_DELAY);
  
    //  ESP_LOGI(TAG, "sending");
     char* data;
    data = format_can_to_string_itoa(can_queue);
   
   
    esp_websocket_client_send_text(client, data, strlen(data), 10000);
    xSemaphoreGive(rx_sem);
    // stack = uxTaskGetStackHighWaterMark(NULL);
    //  ESP_LOGI(TAG, "stack --- %d", stack);
     

    
    

    }

  
}








static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    httpd_handle_t *server = (httpd_handle_t *) ctx;

    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_START");
        ESP_ERROR_CHECK(esp_wifi_connect());
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        ESP_LOGI(TAG, "STA CONNECTED, STARTING WEB SERVER");
        ESP_LOGI(TAG, "Got IP: '%s'",
                ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
                xSemaphoreGive(rx_sem);
                // websocket_app_start();
               


     
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_DISCONNECTED");
        ESP_ERROR_CHECK(esp_wifi_connect());

        /* Stop the web server */
     
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
        .sta = {
             .ssid = EXAMPLE_WIFI_SSID,
            .password = EXAMPLE_WIFI_PASS,
           
        },
    };
    ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void can_receive_task(void *arg)
{

 
   can_with_id message_struct[QUEUE_SIZE_CAN];
   
//    can_with_id *pmessage;
//    pmessage = message_struct;
   
   int count = 0;
esp_err_t test;
    uint32_t alerts;
    BaseType_t stack;



while(1){
    xSemaphoreTake(rx_sem, portMAX_DELAY);
    can_read_alerts(&alerts,0 );
     ESP_LOGI(TAG,"alert----- %d", alerts);

    
    
for (size_t i = 0; i < QUEUE_SIZE_CAN; i++)
{

 
    test = can_receive(&message_struct[i],0);
    
    



//Process received message
// if (test == ESP_OK){
  
//     // count++;
//     // pmessage->x = count;
//     // if (message_struct[i].message.identifier == (uint32_t) 2024U)
//     // {
//     //     ESP_LOGI(TAG,"---------- %d", message_struct[i].message.identifier);
//     //     /* code */
//     // }
    

// // if (  == 0/* condition */)
// // {
// //      ESP_LOGI(TAG,"--------- queue error");
// // }





  
// }
if(test == ESP_OK)
xQueueSend(can_queue, &message_struct[i], 0);
// pmessage++;
    /* code */
}










 
    //  stack = uxTaskGetStackHighWaterMark(NULL);
    //  ESP_LOGI("can", "about to give semaphore");
    xSemaphoreGive(rx_sem);
    vTaskDelay(5);
    
  
}
    
    
}

static void can_send_task(void *arg)
{

   
  static can_message_t tx_message;
  tx_message.identifier = (uint32_t) 0x7E8U;
  tx_message.data[0] = (uint8_t) 2U;
  tx_message.data[1] = (uint8_t) 01U;
  tx_message.data[2] = (uint8_t) 11U;
  tx_message.data_length_code = (uint8_t) 8U;
  esp_err_t test;


while(1){

    test = can_transmit( &tx_message, 0);
      ESP_LOGI(TAG, "sent OBD query %d ", test);
        vTaskDelay(500 / portTICK_PERIOD_MS);
  
}
    
    
}

void app_main()
{

   
    rx_sem = xSemaphoreCreateBinary();
    can_queue=  xQueueCreate(QUEUE_SIZE_CAN, sizeof(can_with_id) );
    xTaskCreatePinnedToCore(can_receive_task, "CAN_rx",               1000+QUEUE_SIZE_CAN*100, NULL, 4, NULL, tskNO_AFFINITY);
    xTaskCreatePinnedToCore(websocket_app_start, "websocket_task",                1000+QUEUE_SIZE_CAN*400, NULL, 4, NULL, tskNO_AFFINITY);
    // xTaskCreatePinnedToCore(can_send_task, "CAN_tx", 1024, NULL, RX_TASK_PRIO, NULL, tskNO_AFFINITY);

    //Install and start CAN driver
    ESP_ERROR_CHECK(can_driver_install(&g_config, &t_config, &f_config));
    ESP_LOGI(EXAMPLE_TAG, "Driver installed");
    ESP_ERROR_CHECK(can_start());
    
    
    ESP_LOGI(EXAMPLE_TAG, "Driver started");
     static httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(nvs_flash_init());
    initialise_wifi(&server);

     

}

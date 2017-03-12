/*
 * socketserver.c
 *
 *  Created on: Feb 28, 2017
 *      Author: dries
 */


#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "freertos/portmacro.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "tcpip_adapter.h"
#include "frameDispatcher.h"
static EventGroupHandle_t wifi_event_group;
const int CONNECTED_BIT = BIT0;
//static char* TAG = "app_main";

#include "lwip/err.h"
#include "string.h"

#include "cJSON.h"

#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/api.h"

#define delay(ms) (vTaskDelay(ms/portTICK_RATE_MS))
char* json_unformatted;

wifi_config_t sta_config;
uint16_t portnumber = 8000;

static void http_server(void *pvParameters);
static void initialise_wifi(void);
static esp_err_t event_handler(void *ctx, system_event_t *event);

const static char http_html_hdr_200[] =
    "HTTP/1.1 200 OK\r\nContent-type: text/html\r\n\r\n";
const static char http_html_hdr_400[] =
    "HTTP/1.1 400 OK\r\nContent-type: text/html\r\n\r\n";


int init_socketserver(wifi_config_t * config, uint16_t port)
{
	//copy wifi settings
	memcpy(&sta_config,config,sizeof(wifi_config_t));
	//set port number
	portnumber = port;

	nvs_flash_init();
    system_init();
    initialise_wifi();
    xTaskCreate((TaskFunction_t)&http_server, "http_server", 2048, NULL, 5, NULL);

    return 0;
}

static void initialise_wifi(void)
{
    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &sta_config) );
    ESP_ERROR_CHECK( esp_wifi_start() );
}

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
        printf("got ip\n");
        printf("ip: " IPSTR "\n", IP2STR(&event->event_info.got_ip.ip_info.ip));
        printf("netmask: " IPSTR "\n", IP2STR(&event->event_info.got_ip.ip_info.netmask));
        printf("gw: " IPSTR "\n", IP2STR(&event->event_info.got_ip.ip_info.gw));
        printf("\n");
        fflush(stdout);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        /* This is a workaround as ESP32 WiFi libs don't currently
           auto-reassociate. */
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        break;
    default:
        break;
    }
    return ESP_OK;
}


static void
http_server_netconn_serve(struct netconn *conn)
{
  struct netbuf *inbuf;
  char *buf;
  u16_t buflen;
  int noc;
  err_t err;
  char respbuf[40] = {0};

  /* Read the data from the port, blocking if nothing yet there.
   We assume the request (the part we care about) is in one netbuf */
  err = netconn_recv(conn, &inbuf);

  if (err == ERR_OK) {
    netbuf_data(inbuf, (void**)&buf, &buflen);

    //printf("buffer = %s \n", buf);

    if((noc = frameDispatcher_json_to_queu(buf))<0){
    	netconn_write(conn, http_html_hdr_400, sizeof(http_html_hdr_400)-1, NETCONN_NOCOPY);
    }
    else{
    	netconn_write(conn, http_html_hdr_200, sizeof(http_html_hdr_200)-1, NETCONN_NOCOPY);
    	sprintf((char *)respbuf,"Number of parsed commands %4d\r\n",noc);
    	netconn_write(conn, respbuf, sizeof(respbuf), NETCONN_NOCOPY);

    }

  }
  /* Close the connection (server closes in HTTP) */
  netconn_close(conn);

  /* Delete the buffer (netconn_recv gives us ownership,
   so we have to make sure to deallocate the buffer) */
  netbuf_delete(inbuf);
}

static void http_server(void *pvParameters)
{
  struct netconn *conn, *newconn;
  err_t err;
  conn = netconn_new(NETCONN_TCP);
  netconn_bind(conn, NULL, portnumber);
  netconn_listen(conn);
  do {
     err = netconn_accept(conn, &newconn);
     if (err == ERR_OK) {
       http_server_netconn_serve(newconn);
       netconn_delete(newconn);
     }
   } while(err == ERR_OK);
   netconn_close(conn);
   netconn_delete(conn);
}




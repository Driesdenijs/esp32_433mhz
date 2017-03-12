/*
 * main.c
 *
 *  Created on: Feb 28, 2017
 *      Author: dries
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "driver/rmt.h"
#include "driver/periph_ctrl.h"
#include "frameDispatcher.h"
#include "socketserver.h"


wifi_config_t sta_config = {
	.sta = {
		.ssid = "MyFi",
		.password = "Java2465",
		.bssid_set = false
	}
};

//void main(){
void app_main(){

	//setup the dispatcher
	xTaskCreate(frameDispatcher_task, "framedisp", 2048, NULL, 10, NULL);

	//init wifi
	init_socketserver(&sta_config, 1000);

	while(1){
		 vTaskDelay(5000 / portTICK_PERIOD_MS);

	};


}

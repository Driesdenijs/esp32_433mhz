/* * frame_dispatcher.c
 *
 *  Created on: Feb 28, 2017
 *      Author: dries
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "stdbool.h"
#include "esp_err.h"
#include "esp_log.h"
#include "cJSON.h"
#include "frameDispatcher.h"
#include "kaku.h"

struct cJSON * json_array;
struct cJSON * json_array_item;
int json_array_size;

QueueHandle_t commandQueuHandle = 0;
cJSON * root;
cJSON * jvalue;
RFcommand queucommand;

static const char* JSON_TAG = "JSON";

int frameDispatcher_json_to_queu(char * json){

	//try to parse json file
    if((root = cJSON_Parse((const char *)json)) == NULL){
    	if(cJSON_GetErrorPtr() != NULL	)
    		ESP_LOGI(JSON_TAG,"error at: %s",cJSON_GetErrorPtr()-5);
    		ESP_LOGI(JSON_TAG,"               ^");
    	return -1;
    }

    cJSON *item = cJSON_GetObjectItem(root,"commands");
    if(item == NULL){
    	ESP_LOGI(JSON_TAG,"tag \"commands\" not found");
    	return(-1);
    }

	int i;
    for (i = 0 ; i < cJSON_GetArraySize(item) ; i++)
    {
    	cJSON * subitem = cJSON_GetArrayItem(item, i);

    	//protocol
    	if((jvalue = cJSON_GetObjectItem(subitem, "protocol")) != NULL){
    		strncpy(queucommand.protocol, jvalue->valuestring,RFCOMMAND_STRING_SIZE);
    	}else{
    		continue;
    	}

    	//type
    	if((jvalue = cJSON_GetObjectItem(subitem, "type")) != NULL){
    		strncpy(queucommand.type, jvalue->valuestring,RFCOMMAND_STRING_SIZE);
    	}else{
    		strncpy(queucommand.type, "dimmer",RFCOMMAND_STRING_SIZE);
    	}

    	//value
    	if((jvalue = cJSON_GetObjectItem(subitem, "value")) != NULL){
    		queucommand.value = jvalue->valueint;
    	}else{
    		continue;
    	}

    	//value
		if((jvalue = cJSON_GetObjectItem(subitem, "unit")) != NULL){
			queucommand.unit = jvalue->valueint;
		}else{
			continue;
		}

    	//address
    	if((jvalue = cJSON_GetObjectItem(subitem, "address")) != NULL){
    		queucommand.address = jvalue->valueint;
    	}else{
    		continue;
    	}

    	//value
    	if((jvalue = cJSON_GetObjectItem(subitem, "repeat")) != NULL){
    		queucommand.repetitions = jvalue->valueint;
    	}else{
    		queucommand.repetitions = 25;
    	}

    	//printf("queued: protocol %s value:%2i addr:%i type %s\n",queucommand.protocol,queucommand.value, queucommand.address,queucommand.type);

    	xQueueGenericSend(commandQueuHandle, &queucommand, 1000, queueSEND_TO_BACK);
    }

    return cJSON_GetArraySize(item);
}

void frameDispatcher_task()
{
	RFcommand queucommand;

	//set debug for json
	esp_log_level_set(JSON_TAG, ESP_LOG_INFO);
	ESP_LOGI(JSON_TAG,"cJSON version:%s",cJSON_Version());
	//create command queue
	commandQueuHandle = xQueueCreate( 10, sizeof(queucommand));

	for(;;){
		if(xQueueGenericReceive(commandQueuHandle,&queucommand, 10000 , false)){
			//ESP_LOGI(JSON_TAG,"Enqueued item with protocol \"%s\"",queucommand.protocol);
			if(strcmp(queucommand.protocol , "kaku\0") == 0 )
			{
				kaku_sendframe(queucommand);

			}
		}
		//ESP_LOGI(JSON_TAG,"Nothing to enqueued");
	}

}



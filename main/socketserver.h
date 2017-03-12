/*
 * socketserver.h
 *
 *  Created on: Mar 5, 2017
 *      Author: dries
 */

#ifndef MAIN_SOCKETSERVER_H_
#define MAIN_SOCKETSERVER_H_
#include "esp_wifi_types.h"


int init_socketserver(wifi_config_t * config , uint16_t portnumber);

#endif /* MAIN_SOCKETSERVER_H_ */

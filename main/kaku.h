/*
 * kaku.h
 *
 *  Created on: Mar 5, 2017
 *      Author: dries
 */

#ifndef MAIN_KAKU_H_
#define MAIN_KAKU_H_


typedef struct {
	union {
		struct {
			uint32_t unit    :4;	  //specific unit [0...15]
			uint32_t on_off  :1;	  // desired state for on off devices else [dim_value]
			uint32_t group   :1;	  //'1' means the state is for the whole group, i.e. all on
			uint32_t address :26; 	  //unique address
		};
		uint32_t address_state;
	};
	uint8_t value;
} kaku_frame;

void kaku_sendframe(RFcommand command);

#endif /* MAIN_KAKU_H_ */

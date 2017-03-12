/*
 * frameDispatcher.h
 *
 *  Created on: Mar 4, 2017
 *      Author: dries
 */

#ifndef MAIN_FRAMEDISPATCHER_H_
#define MAIN_FRAMEDISPATCHER_H_

#define RFCOMMAND_STRING_SIZE 16

typedef struct {
		char protocol[RFCOMMAND_STRING_SIZE];
		char type[RFCOMMAND_STRING_SIZE];
		int address;
		int unit;
		int value;
		int repetitions;
}RFcommand;


int frameDispatcher_json_to_queu(char * json);
void frameDispatcher_task();


#endif /* MAIN_FRAMEDISPATCHER_H_ */

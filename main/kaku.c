#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/rmt.h"
#include "driver/periph_ctrl.h"
#include "soc/rmt_reg.h"
#include "frameDispatcher.h"
#include "kaku.h"

static const char* KAKU_TAG = "KAKU";

#define KAKU_GROUP				0
#define KAKU_STATE				1
#define KAKU_UNIT				3
#define KAKU_ADDRESS_STATUS     0x503F3290ul
#define KAKU_ADDRESS			(KAKU_ADDRESS_STATUS >> 6ul)

#define KAKU_ON					(x | 0x00000010ul)
#define KAKU_OFF				(x & 0xFFFFFFEFul)

#define KAKU_MINIMAL_MSSG_SIZE  32				/*!< 32  without dim 36 with dim*/

#define KAKU_BIT_SHORT_HIGH		221              /*!< KAKU protocol data bit : positive 0.275ms */
#define KAKU_BIT_SHORT_LOW		321              /*!< KAKU protocol data bit : positive 0.275ms */
#define KAKU_BIT_LONG			1331              /*!< KAKU protocol data bit : positive 0.275ms */

#define KAKU_START_HIGH			KAKU_BIT_SHORT_HIGH
#define KAKU_START_LOW			2724
#define KAKU_STOP_HIGH			KAKU_BIT_SHORT_HIGH
#define KAKU_STOP_LOW			10320

#define RMT_TX_CARRIER_EN    0   /*!< Enable carrier for IR transmitter test with IR led */


#define RMT_TX_CHANNEL    1     /*!< RMT channel for transmitter */
#define RMT_TX_GPIO_NUM   13     /*!< GPIO number for transmitter signal */
#define RMT_CLK_DIV       100    /*!< RMT counter clock divider */
#define RMT_TICK_10_US    (80000000/RMT_CLK_DIV/100000)   /*!< RMT counter value for 10 us.(Source clock is APB clock) */

enum deviceTypes
{
	KAKU_SWITCH = 0,
	KAKU_DIMMER,
	KAKU_ETC
};


/*
 * @brief RMT transmitter initialization
 */
static void kaku_init()
{
    rmt_config_t rmt_tx;
    rmt_tx.channel = RMT_TX_CHANNEL;
    rmt_tx.gpio_num = RMT_TX_GPIO_NUM;
    rmt_tx.mem_block_num = 1;
    rmt_tx.clk_div = RMT_CLK_DIV;
    rmt_tx.tx_config.loop_en = false;
    rmt_tx.tx_config.carrier_duty_percent = 50;
    rmt_tx.tx_config.carrier_freq_hz = 38000;
    rmt_tx.tx_config.carrier_level = 1;
    rmt_tx.tx_config.carrier_en = RMT_TX_CARRIER_EN;
    rmt_tx.tx_config.idle_level = 1;
    rmt_tx.tx_config.idle_output_en = true;
    rmt_tx.rmt_mode = 0;
    rmt_config(&rmt_tx);
    rmt_driver_install(rmt_tx.channel, 0, 0);

    esp_log_level_set(KAKU_TAG, ESP_LOG_INFO);
}



/*
 * @brief Build register value of waveform for one data bit
 */
inline void kaku_fill_item_level(rmt_item32_t* item, int high_us, int low_us)
{
    item->level0 = 1;
    item->duration0 = (high_us) / 10 * RMT_TICK_10_US;
    item->level1 = 0;
    item->duration1 = (low_us) / 10 * RMT_TICK_10_US;
}

/*
 *  @brief
 *           _      _
 *  '1':	| |____| |_	(T,3T,T,T)
 */
static int kaku_onePulse(rmt_item32_t *item)
{
	kaku_fill_item_level( item, KAKU_BIT_SHORT_HIGH , KAKU_BIT_LONG);
	kaku_fill_item_level( item + 1, KAKU_BIT_SHORT_HIGH , KAKU_BIT_SHORT_LOW);
	return 2;
}
/*
 *	@brief
 *	         _   _
 *	'0':	| |_| |____	(T,T,T,3T)
 */
static int kaku_zeroPulse(rmt_item32_t *item)
{
	kaku_fill_item_level( item, KAKU_BIT_SHORT_HIGH , KAKU_BIT_SHORT_LOW);
	kaku_fill_item_level( item + 1, KAKU_BIT_SHORT_HIGH , KAKU_BIT_LONG);
	return 2;
}

/*
 *	@brief
 *	         _   _
 *	DIM:	| |_| |_	(T,T,T,T)
 */
static int kaku_dimPulse(rmt_item32_t *item)
{
	kaku_fill_item_level( item, KAKU_BIT_SHORT_HIGH , KAKU_BIT_SHORT_LOW);
	kaku_fill_item_level( item + 1, KAKU_BIT_SHORT_HIGH , KAKU_BIT_SHORT_LOW);
	return 2;
}

/*
 *  @brief
 *       _
 *  ST:	| |_______	(T,10T)
 */
static int kaku_startPulse(rmt_item32_t *item)
{
	kaku_fill_item_level( item, KAKU_START_HIGH , KAKU_START_LOW);
	return 1;
}

/*
 *  @brief
 *       _
 *  SP:	| |____...	(T,40T)
 */
static int kaku_stopPulse(rmt_item32_t *item)
{
	kaku_fill_item_level( item, KAKU_STOP_HIGH , KAKU_STOP_LOW);
	return 1;
}


/*
 * @brief Build kaku frame
 */
static int kaku_build_frame(rmt_item32_t* item, kaku_frame * frame )
{
    int i = 0;
    rmt_item32_t* start_item = item;
    uint32_t addr_state = frame->address_state;

    //add start pulse
    item += kaku_startPulse(item);

    //add address and state in one go 32 bits
    //ESP_LOGI(KAKU_TAG, "0x%08x 0x%08x",frame->address_state,frame->address);
    for(i = 0; i < 27 ; i++) {
        item += ((addr_state <<i) & 0x80000000ul)? kaku_onePulse(item) : kaku_zeroPulse(item);
    }

    //ESP_LOGI(KAKU_TAG, "%d" ,frame->dim_value);
    // dim or not to dim...
    if(frame->value == 0x00){
    	//if dimmer is full on or full off ignore dim value and write the last bit off address_state as usual
    	item += frame->on_off ? kaku_onePulse(item) : kaku_zeroPulse(item);
    }else{
    	//to enter dimmer mode the last bit of the address_state needs to be different
    	item += kaku_dimPulse(item);
    }

    //uint number
	for(i = 0; i < 4; i++) {
		item +=((frame->unit << i) & 0x08)? kaku_onePulse(item): kaku_zeroPulse(item);
	}

	//add the dim bits (16 levels)
	if(frame->value != 0){
		//add dim value if not 0x00 or 0x0F (full off or full on, is just regular on off)
		for(i = 0; i < 4; i++) {
			item += ((frame->value << i) & 0x08)? kaku_onePulse(item): kaku_zeroPulse(item);
		}
	}

    //close the frame with a stop pulse
	kaku_stopPulse(item);

    return (item - start_item)+1;
}

/**
 * @brief RMT transmitter demo, this task will periodically send NEC data. (100 * 32 bits each time.)
 *
 */
void kaku_sendframe(RFcommand command)
{
	int x;

    //parse the command struct to a kaku
    kaku_frame frame ={
    		.address = command.address,
			.unit = command.unit,
    		.value = command.value
    };

    //verify some limits
    if(frame.unit > 15 || frame.unit < 0)frame.unit = frame.unit%16;
    if(frame.value > 15 )frame.value = frame.value%16;
    if(frame.unit > 15 )frame.unit = frame.unit%16;
    if(command.repetitions > 100)command.repetitions = 100;
    if(command.repetitions < 1)command.repetitions = 25;

    kaku_init();

	//allocate pulse memory
	rmt_item32_t* item = (rmt_item32_t*) malloc(100*sizeof(rmt_item32_t));
	memset(item, 0, 100*sizeof(rmt_item32_t));
	int size = kaku_build_frame( item, &frame );

	//ESP_LOGI(KAKU_TAG, "framesize %2d -address 0x%08x dim %2d unit %d group %d repetitions %d\n", size ,frame.address_state,frame.value, frame.unit ,frame.group, command.repetitions);
	for(x=0;x<command.repetitions;x++){
		//To send data according to the waveform items.
		rmt_write_items(RMT_TX_CHANNEL, item, 100, true);
		//Wait until sending is done.
		rmt_wait_tx_done(RMT_TX_CHANNEL);
	}
	//before we free the data, make sure sending is already done.
	free(item);
}

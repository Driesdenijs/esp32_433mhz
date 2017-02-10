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

static const char* KAKU_TAG = "KAKU";

#define KAKU_GROUP				0
#define KAKU_STATE				1
#define KAKU_UNIT				0
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
	uint8_t dim_value;
} kaku_frame;


/*
 * @brief RMT transmitter initialization
 */
static void rmt_tx_init()
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
static int kaku_build_frame(rmt_item32_t* item, kaku_frame* frame )
{
    int i = 0;
    rmt_item32_t* start_item = item;
    uint32_t addr_state = frame->address_state;
    uint8_t dim = frame->dim_value & 0x0F;

    //add start pulse
    item += kaku_startPulse(item);

    //add address and state in one go 32 bits
    //ESP_LOGI(KAKU_TAG, "0x%08x 0x%08x",frame->address_state,frame->address);
    for(i = 0; i < 32 ; i++) {
        if(addr_state & 0x80000000ul) {
        	//ESP_LOGI(KAKU_TAG, "%2d - 1",i+1);
        	item += kaku_onePulse(item);
        } else {
        	//ESP_LOGI(KAKU_TAG, "%2d - 0",i+1);
        	item += kaku_zeroPulse(item);
        }
        addr_state <<= 1;
    }

    ESP_LOGI(KAKU_TAG, "%d" ,dim);
    // dim or not to dim...
    if((dim  == 0x00) || (dim == 0x0F)){
    	//if dimmer is full on or full off ignore dim value and write the last bit off address_state as usual
    	if(addr_state & 0x1) {
    	   	item += kaku_onePulse(item);
    	} else {
    	    item += kaku_zeroPulse(item);
    	}
    }else{
    	//to entre dimmer mode the last bit of the address_state needs to be different
    	item += kaku_dimPulse(item);

		//add dim value if not 0x00 or 0x0F (full off or full on, is just regular on off)
		for(i = 0; i < 4; i++) {
			if(dim & 0x08) {
				item += kaku_onePulse(item);
			} else {
				item += kaku_zeroPulse(item);
			}
			dim <<= 1;
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
void rmt_kaku_tx_task()
{
	int x;
	static int group =0;
	vTaskDelay(10);
    rmt_tx_init();
    esp_log_level_set(KAKU_TAG, ESP_LOG_INFO);
    int channel = RMT_TX_CHANNEL;
    ESP_LOGI(KAKU_TAG, "KAKU TRANSMIT START");
    kaku_frame frame =
    {
    		//.address_state = KAKU_ADDRESS_STATUS
    		.address = KAKU_ADDRESS,
    		.group = KAKU_GROUP,
			.on_off = KAKU_STATE,
			.unit = KAKU_UNIT,
			.dim_value = 0
    };
    frame.address_state &= 0xFFFFFFC0ul;

    for(;;) {
    	frame.dim_value = group++%16;
        //if(frame.on_off)frame.on_off=0;
        //else frame.on_off = 1;
    	rmt_item32_t* item = (rmt_item32_t*) malloc(100*sizeof(rmt_item32_t));
        bzero(item, 70*sizeof(rmt_item32_t));
        int size = kaku_build_frame( item, &frame );
        for(x=0;x<25;x++){
        	//To send data according to the waveform items.
        	rmt_write_items(channel, item, 100, true);
        	//Wait until sending is done.
        	rmt_wait_tx_done(channel);
        }
        //before we free the data, make sure sending is already done.
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        free(item);
    }
    vTaskDelete(NULL);
}

        //for(x=0;x < 70 ; x++){
        	//ESP_LOGI(KAKU_TAG, "%2d = %d %5d - %2d %5d",x, item[x].level0, item[x].duration0,item[x].level1, item[x].duration1);
        //}

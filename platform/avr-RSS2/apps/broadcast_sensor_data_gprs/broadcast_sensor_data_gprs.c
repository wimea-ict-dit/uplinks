/*
  Contiki Rime BRD demo.
  Broadcast Temp, Bat Voltage, RSSI, LQI DEMO. Robert Olsson <robert@herjulf.se>

  Heavily based on code from:

 * Copyright (c) 2007, Swedish Institute of Computer Science.
 * All rights reserved.
 *
  See Contiki for full copyright.
*/

//#include "lib/random.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <string.h>

#include <avr/sleep.h>
#include <avr/wdt.h>
#include "contiki.h"
#include "contiki-net.h"
#include "lib/list.h"
#include "lib/memb.h"
#include "net/rime/rime.h"
#include "random.h"

//#include <time.h>
#include "rss2.h"
#include <avr/io.h>
#include "sys/etimer.h"
#include "adc.h"
#include "i2c.h"
#include "dev/leds.h"
#include "dev/battery-sensor.h"
#include "dev/temp-sensor.h"
#include "dev/temp_mcu-sensor.h"
#include "dev/light-sensor.h"
#include "dev/pulse-sensor.h"
#include "shell.h"
#include "serial-shell.h"
#include "dev/serial-line.h"
#ifdef CO2
#include "dev/co2_sa_kxx-sensor.h"
#endif

#define MAX_NEIGHBORS 64
#define SIZE          40
#define SHELL_INTERFACE 1
#define SEQNO_EWMA_UNITY 0x100  
#define SEQNO_EWMA_ALPHA 0x040
#define END_OF_FILE 26
#define MAX_BCAST_SIZE 99
#define DEF_TTL 0xF
#define DEF_CHAN 26

unsigned char charbuf[SIZE];
uint8_t eof = END_OF_FILE;
uint16_t h,m;
double v_avr;
double temp;
uint8_t  ttl = DEF_TTL;

//cdeclare structures
struct broadcast_message {
  uint8_t head;                    /* version << 4 + ttl */
  uint8_t seqno;
  uint8_t buf[MAX_BCAST_SIZE+20];  /* Check for max payload 20 extra to be able to test */
};

struct {
  uint16_t sent;
  uint16_t dup;
  uint16_t ignored;
  uint16_t ttl_zero;
} relay_stats;

struct neighbor {
  struct neighbor *next;
  linkaddr_t addr;
  uint16_t last_rssi, last_lqi;
  uint8_t last_seqno;
  uint32_t avg_seqno_gap;
};

MEMB(neighbors_memb, struct neighbor, MAX_NEIGHBORS);
LIST(neighbors_list);

static struct broadcast_conn broadcast;

PROCESS(init_process, "Init process");

PROCESS(broadcast_process, "Broadcast process");

#ifdef SHELL_INTERFACE
PROCESS(rint_process , "Set Report Interval");
SHELL_COMMAND( rint_command, 
               "rint", 
               "rint <number> [Here the \"number\" represents the report periodicity in seconds]", 
               &rint_process) ;

PROCESS( rmask_process , "Set Report Mask");
SHELL_COMMAND( rmask_command, 
               "rmask", 
               "rmask <number> [Here the \"number\" denotes the 8 bit mask for the data fields in the Sensor Report]",
               &rmask_process) ;

PROCESS(upgr_process , "Bootloader Start");
SHELL_COMMAND( upgr_command, 
               "upgr", 
               "upgr  [Starts the bootloader]", 
               &upgr_process) ;

#endif

PROCESS(test_serial, "Serial line test process");

AUTOSTART_PROCESSES( &init_process,&test_serial, &broadcast_process);


void print_help_command_menu(){

 printf("\n------------------Command Menu--------------------------------\n") ;
 printf("\nh\tPrints a list of supported commands\n\t Usage: h\n") ;
 printf("ri\tSets the report interval\n\t  Usage: ri <period in seconds>\n");
 printf("tagmask\tSets the report tag mask\n\t       Usage: tagmask <1 byte number representing the mask>\n");
 printf("tagmask bits correspond to the following tags MSB -> LS,\n| UT | PS | T | T_MCU | V_MCU | UP | V_IN | spare bit |\n") ;
 printf("upgr\tInitiates bootloader\n\t  Usage: upgr\n");
 printf("---------------------------------------------------------------\n\n");

}

/*
We have a set of tags that identify the data items in each report.
Tagmask is an 8 bit character whose bits are mapped to the report tags
and by setting or resetting these bits the report size can be manipuated

         MSB                                          LSB
   bits:  7    6    5     4      3      2       1      0
        ---------------------------------------------------
        | UT | PS | T | T_MCU | V_MCU | UP | V_IN | spare |
        ---------------------------------------------------

    date time and ID tag is mandatory and it will come in every report

    VALID VALUES:
        0xFB which enables all these tags.
        A report set by 0xFB                              
	2012-05-22 14:07:46 UT=1337688466 ID=283c0cdd030000d7 PS=0 T=30.56  T_MCU=34.6  V_MCU=3.08 UP=2C15C V_IN=4.66 
*/

// Bitwise structure for mapping the input mask - maneesh
struct tag_mask {
	unsigned ut:1 ;
        unsigned ps:1 ;
	unsigned t:1 ;
	unsigned t_mcu:1 ;
	unsigned v_mcu:1 ;
	unsigned up:1 ;
	unsigned v_in:1 ;
	unsigned spare:1 ;    
} tagmask ;

unsigned char eui64_addr[8] ;
unsigned int report_interval = 5 ;

PROCESS_THREAD(test_serial, ev, data)
{
   PROCESS_BEGIN();

   char delimiter[] = " ";
   char *command = NULL ;
   char *temp = NULL ;
   unsigned char  ri = 0 ; 

   for(;;) {
     printf("Waiting for EV = %d to be equal to  serial_line_event_message = %d\n",ev, serial_line_event_message);
     PROCESS_YIELD_UNTIL(ev == serial_line_event_message);
     printf("Serial Line Test Process Polled:  EV = %d\n",ev) ;

// debug prints
     if(ev == serial_line_event_message) {
       printf("received line: %s\n", (char *)data);
     } else {
 	printf("Stil: no serial_line_event_message: received Event Number = %d\n", ev) ;
     }
      
     command = (char*)strtok((char*)data, (const char*)delimiter) ;		

     if(!strncmp(command, "h",1)){
	print_help_command_menu() ;
     } else if(!strncmp(command,"ri",2)) {
	temp = strtok(data, delimiter) ;
	report_interval = atoi(temp) ;
     } else if(!strncmp(command, "tagmask",7)){
	temp = strtok(data, delimiter) ;
        ri  = atoi(temp[0]) ; 
        memcpy( (void*)&tagmask, (const void*)&ri , sizeof(ri) )  ; // the tagmask is just 8 bit long ! 
     } else {
	printf("Invalid command %s. Try h for a list of commands\n", command) ;
     }	// more commands can be added here

   }
   	
   PROCESS_END();
}

int radio_sleep = 0;

void read_sensors(void)
{
}

static void broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from)
{
  struct neighbor *n;
  struct broadcast_message *msg;
  uint8_t seqno_gap;
  uint8_t relay = 1;   /* Default on */

  leds_on(LEDS_RED);
  msg = packetbuf_dataptr();

  if((msg->head >> 4) != 1 || (msg->head & 0xF) == 0) {
    relay = 0;
    relay_stats.ignored++;
    goto out;
  }

  /* From our own address. Can happen if we receive own pkt via relay
     Ignore
  */

  if(linkaddr_cmp(&linkaddr_node_addr, from)) {
    relay = 0;
    relay_stats.ignored++;
    goto out;
  }

  /* Check if we already know this neighbor. */
  for(n = list_head(neighbors_list); n != NULL; n = list_item_next(n)) {

    if(linkaddr_cmp(&n->addr, from)) 
      break;
  }

  if(n == NULL) {
    n = memb_alloc(&neighbors_memb); /* New neighbor */

    if(! n ) 
      goto out;
    
    linkaddr_copy(&n->addr, from);
    n->last_seqno = msg->seqno - 1;
    n->avg_seqno_gap = SEQNO_EWMA_UNITY;
    list_add(neighbors_list, n);
  }

  n->last_rssi = packetbuf_attr(PACKETBUF_ATTR_RSSI);
  n->last_lqi = packetbuf_attr(PACKETBUF_ATTR_LINK_QUALITY);

  /* Compute the average sequence number gap from this neighbor. */
  seqno_gap = msg->seqno - n->last_seqno;

  
  n->avg_seqno_gap = (((uint32_t)seqno_gap * SEQNO_EWMA_UNITY) *
                      SEQNO_EWMA_ALPHA) / SEQNO_EWMA_UNITY +
                      ((uint32_t)n->avg_seqno_gap * (SEQNO_EWMA_UNITY -
                                                     SEQNO_EWMA_ALPHA)) / SEQNO_EWMA_UNITY;

  n->last_seqno = msg->seqno;

  printf("&: %s [ADDR=%-d.%-d SEQ=%-d TTL=%-u RSSI=%-u LQI=%-u DRP=%-d.%02d]\n",
	 msg->buf,
	 from->u8[0], from->u8[1], msg->seqno, msg->head & 0xF,
	 n->last_rssi,
	 n->last_lqi, 
	 (int)(n->avg_seqno_gap / SEQNO_EWMA_UNITY),
	 (int)(((100UL * n->avg_seqno_gap) / SEQNO_EWMA_UNITY) % 100));

out:
  leds_off(LEDS_RED);
}


static const struct broadcast_callbacks broadcast_call = {broadcast_recv};

/*
  Channel 11 - 26 

  MAX TX_PWR_3DBM       ( 0 )
  MIN TX_PWR_17_2DBM    ( 15 )

  Channels (11, 15, 20, 25 are WiFi-free).
  RSSI is high while LQI is low -> Strong interference, 
  Both are low -> Try different locations.
*/

PROCESS_THREAD(broadcast_process, ev, data)
{
  static uint8_t seqno;
  struct broadcast_message msg;
  uint8_t ch, txpwr;

  PROCESS_EXITHANDLER(broadcast_close(&broadcast);)
  PROCESS_BEGIN();
  broadcast_open(&broadcast, 129, &broadcast_call);

  while(1) {
    int len;

    //etimer_set(&et, CLOCK_SECOND * 2);
    //PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
    PROCESS_YIELD_UNTIL(ev == 0x12);

    leds_on(LEDS_YELLOW);

    read_sensors();

    len = 0;
    
    // write the EUI64 MAC address to the message
    len += sprintf(&msg.buf[len] , "E64=%02x%02x%02x%02x%02x%02x%02x%02x ", eui64_addr[0],eui64_addr[1], eui64_addr[2],
                                       eui64_addr[3], eui64_addr[4], eui64_addr[5], eui64_addr[6], eui64_addr[7] ) ;
    
    if( tagmask.t_mcu ){
    	len += sprintf(&msg.buf[len], "T_MCU=%-5.1f ",temp);
    }
    if( tagmask.v_mcu){
    	len += sprintf(&msg.buf[len], "V_MCU=%-4.2f ",v_avr);
    }

    len += snprintf((char *) &msg.buf[len], sizeof(msg.buf), "&: ");
    len += snprintf((char *) &msg.buf[len], sizeof(msg.buf), "TXT=ERICSON-6LOWPAN ");
    len += snprintf((char *) &msg.buf[len], sizeof(msg.buf), "TARGET=avr-rss2 ");
    len += snprintf((char *) &msg.buf[len], sizeof(msg.buf), "V_MCU=%-d ", battery_sensor.value(0));
    len += snprintf((char *) &msg.buf[len], sizeof(msg.buf), "T_MCU=%-d ", temp_mcu_sensor.value(0));
    len += snprintf((char *) &msg.buf[len], sizeof(msg.buf), "LIGHT=%-d ", light_sensor.value(0));
#ifdef CO2
    len += snprintf((char *) &msg.buf[len], sizeof(msg.buf), "CO2=%-d ", co2_sa_kxx_sensor.value(CO2_SA_KXX_CO2));
#endif
    len += snprintf((char *) &msg.buf[len], sizeof(msg.buf), "\n\r");

    // todo print the other data items as per their tagmasks here !!! - maneesh
    msg.buf[len++] = 0;
    packetbuf_copyfrom(&msg, len+2);

    msg.head = 1<<4; /* Version 1 */
    msg.head |= ttl;;
    msg.seqno = seqno;

    broadcast_send(&broadcast);
    seqno++;
    
    printf("&: %s\n", msg.buf);

    leds_off(LEDS_YELLOW);
  }
  PROCESS_END();
}

PROCESS_THREAD(init_process, ev, data)
{
  PROCESS_BEGIN();

  SENSORS_ACTIVATE(temp_sensor);
  SENSORS_ACTIVATE(battery_sensor);
  SENSORS_ACTIVATE(temp_mcu_sensor);
  SENSORS_ACTIVATE(light_sensor);
  SENSORS_ACTIVATE(pulse_sensor);
#ifdef CO2
  SENSORS_ACTIVATE(co2_sa_kxx_sensor);
#endif
  leds_init(); 
  leds_on(LEDS_RED);
  leds_on(LEDS_YELLOW);

// Registering the shell commands - maneesh 14/12/2013	 
#ifdef SHELL_INTERFACE 
  serial_shell_init();
  printf("serial_shell_init()\n");
  shell_register_command(&rint_command);
  printf("registered rint command\n");
  shell_register_command(&rmask_command);
  printf("registered rmask command\n"); 
  shell_register_command(&upgr_command);
  printf("registered upgr command\n");
#endif 

  //////get_eui64_addr(eui64_addr) ;
  memset(&tagmask,0x18, 1) ; // enable t_mcu, v_mcu tags
   
  PROCESS_END();
}

#ifdef SHELL_INTERFACE
PROCESS_THREAD(rint_process, ev, data)
{
  PROCESS_BEGIN();
  
  printf("Shell Command \"rint\" invoked\n");

  PROCESS_END() ; 
}

PROCESS_THREAD(rmask_process, ev, data)
{
  PROCESS_BEGIN();
  printf("Shell Command \"rmask\" invoked\n") ;
  PROCESS_END() ;
}

PROCESS_THREAD(upgr_process, ev, data)
{
  PROCESS_BEGIN();
  printf("OK\n");
  printf("%c", eof);
  wdt_enable(WDTO_15MS);
  while (1);
  PROCESS_END() ; 
}

#endif


static struct pt send_thread;

PT_THREAD(send_message(char *txt, linkaddr_t b, int *retval))
{
  PT_BEGIN(&send_thread);

    printf("%s \n", txt);

  *retval = 0;
  PT_END(&send_thread);
}

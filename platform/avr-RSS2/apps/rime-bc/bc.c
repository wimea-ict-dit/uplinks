/*
  Contiki Rime BRD demo.
  Broadcast Temp, Bat Voltage, RSSI, LQI DEMO. Robert Olsson <robert@herjulf.se>

  Heavily based on code from:

 * Copyright (c) 2007, Swedish Institute of Computer Science.
 * All rights reserved.
 *
  See Contiki for full copyright.
*/

#include <avr/sleep.h>
#include "contiki.h"
#include "contiki-net.h"
#include "lib/list.h"
#include "lib/memb.h"
#include "lib/random.h"
#include "net/rime.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
//#include <time.h>
#include "rss2.h"
#include <avr/io.h>
#include "shell.h"
#include "serial-shell.h"
#include "dev/serial-line.h"

#define MAX_NEIGHBORS 64
#define SIZE          40

unsigned char charbuf[SIZE];

uint16_t h,m;
double v_avr;
double temp;

#define MAX_BCAST_SIZE 99

struct broadcast_message {
  uint8_t head; /* version << 4 + ttl */
  uint8_t seqno;
  uint8_t buf[MAX_BCAST_SIZE+20];  /* Check for max payload 20 extra to be able to test */
};

#define DEF_TTL 0xF
uint8_t  ttl = DEF_TTL;

#define DEF_CHAN 26

struct {
  uint16_t sent;
  uint16_t dup;
  uint16_t ignored;
  uint16_t ttl_zero;
} relay_stats;

struct neighbor {
  struct neighbor *next;
  rimeaddr_t addr;

  /* The ->last_rssi and ->last_lqi fields hold the Received Signal
     Strength Indicator (RSSI) and CC2420 Link Quality Indicator (LQI)
     values that are received for the incoming broadcast packets. */
  uint16_t last_rssi, last_lqi;
  uint8_t last_seqno;

  /* The ->avg_gap contains the average seqno gap that we have seen
     from this neighbor. */
  uint32_t avg_seqno_gap;
};

MEMB(neighbors_memb, struct neighbor, MAX_NEIGHBORS);
LIST(neighbors_list);

static struct broadcast_conn broadcast;

#define SEQNO_EWMA_UNITY 0x100 /* Moving average */
#define SEQNO_EWMA_ALPHA 0x040

PROCESS(init_process, "Init process");
PROCESS(broadcast_process, "Broadcast process");

/* The Shell commands initialised here
 * 1. The Report Interval Command rint <n> where n represents periodicity in seconds 
 * 2. The Report Mask command rmask <n> where n represents the 8 bit mask for data fields
 *    in the broadcast report   
*/

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
               
//PROCESS(rled, "Turn on led");
//SHELL_COMMAND(rled_command,
//              "rled",
//              "rled",
//              &rled_process);
#endif

/* -----------------------------------------------------------------------------------------------------------------*/

//AUTOSTART_PROCESSES(&init_process, &broadcast_process);


PROCESS(test_serial, "Serial line test process");
AUTOSTART_PROCESSES( &init_process,&test_serial, &broadcast_process);



void print_help_command_menu(){

 printf("\n------------------Command Menu--------------------------------\n") ;
 printf("\nh\tPrints a list of supported commands\n\t Usage: h\n") ;
 printf("ri\tSets the report interval\n\t  Usage: ri <period in seconds>\n");
 printf("tagmask\tSets the report tag mask\n\t       Usage: tagmask <1 byte number representing the mask>\n");
 printf("tagmask bits correspond to the following tags MSB -> LS,\n| UT | PS | T | T_MCU | V_MCU | UP | V_IN | spare bit |\n") ;

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

   printf("Process begins for test_serial !! \n") ;
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

#define RTC_SCALE 30

static void rtcc_init(void)
{
  TIMSK2 &=~((1<<TOIE2)|(1<<OCIE2A));  /* Disable TC0 interrupt */
  ASSR |= (1<<AS2); /* set Timer/Counter0 to be asynchronous */
  TCNT2 = 0x00;
  //TCCR2B = 0x05;  /* prescale the timer to / 128 to --> 1s */
  TCCR2B = 0x02;  /* prescale the timer to / 8   16 Hz*/
  while(ASSR&0x07);
  TIMSK2 |= (1<<TOIE2);  
}


ISR(TIMER2_OVF_vect)
{ 
  static int rtc = RTC_SCALE;
  PORTE |= (1<<LED_RED);
  PORTE |= (1<<LED_YELLOW);
  if (--rtc == 0 ) {
    static int i;
    rtc = RTC_SCALE;
    if(!(i++ % report_interval)) 
      process_post(&broadcast_process, 0x12, NULL);
  }
}



void read_sensors(void)
{
  uint16_t s;
  uint8_t p1;
  
  BATMON = 16; //Stabilize at highest range and lowest voltage

  ADCSRB |= (1<<MUX5);   //this bit buffered till ADMUX written to!
  ADMUX = 0xc9;         // Select internal 1.6 volt ref, temperature sensor ADC channel
  ADCSRA = 0x85;        //Enable ADC, not free running, interrupt disabled, clock divider 32 (250 KHz@ 8 MHz)
  ADCSRA |= (1<<ADSC);         //Throwaway conversion
  while 
    (ADCSRA &( 1<<ADSC) ); //Wait
  ADCSRA |= (1<<ADSC);          //Start conversion
  while 
    (ADCSRA & (1<<ADSC) ); //Wait
  h = ADC;
  temp = (double) h * 1.13 - 272.8;
  h = 11*h-2728+(h>>2);       //Convert to celcius*10 (should be 11.3*h, approximate with 11.25*h)
  ADCSRA = 0;               //disable ADC
  ADMUX = 0;                //turn off internal vref      
  m = h/10; s=h-10*m;

  for ( p1=16; p1<31; p1++) { 
    BATMON = p1;
    /*  delay_us(100); */
    if ((BATMON & (1<<BATMON_OK)) == 0 ) 
      break;
  }
  v_avr = (double) 2550-75*16-75 + 75 * p1; //-75 to take the floor of the 75 mv transition window
  v_avr = v_avr/1000;
}

static void broadcast_recv(struct broadcast_conn *c, const rimeaddr_t *from)
{
  struct neighbor *n;
  struct broadcast_message *msg;
  uint8_t seqno_gap;
  uint8_t relay = 1;   /* Default on */

  PORTE &= ~LED_RED;  /* ON */
  msg = packetbuf_dataptr();

  if((msg->head >> 4) != 1 || (msg->head & 0xF) == 0) {
    relay = 0;
    relay_stats.ignored++;
    goto out;
  }

  /* From our own address. Can happen if we receive own pkt via relay
     Ignore
  */

  if(rimeaddr_cmp(&rimeaddr_node_addr, from)) {
    relay = 0;
    relay_stats.ignored++;
    goto out;
  }

  /* Check if we already know this neighbor. */
  for(n = list_head(neighbors_list); n != NULL; n = list_item_next(n)) {

    if(rimeaddr_cmp(&n->addr, from)) 
      break;
  }

  if(n == NULL) {
    n = memb_alloc(&neighbors_memb); /* New neighbor */

    if(! n ) 
      goto out;
    
    rimeaddr_copy(&n->addr, from);
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
  PORTE |= LED_RED;  /* OFF */
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

  //rf230_set_txpower(15);
  txpwr = rf230_get_txpower();
  //ch = rf230_get_channel();
  ch = DEF_CHAN;
  rf230_set_channel(ch);
  printf("Ch=%-d TxPwr=%-d\n", ch, txpwr);

// Just read the EUI64 address and the serial number from the at24mac chip
//  at24mac_read(1,1,0) ;

  while(1) {
    int len;

    //etimer_set(&et, CLOCK_SECOND * 2);
    //PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
    PROCESS_YIELD_UNTIL(ev == 0x12);

    PORTE &= ~(1<<LED_YELLOW); /* On */

    read_sensors();

    len = 0;
    
    // write the EUI64 MAC address to the message
    len += sprintf(&msg.buf[len] , "E64=%02x%02x%02x%02x%02x%02x%02x%02x ", eui64_addr[0],eui64_addr[1], eui64_addr[2],
                                       eui64_addr[3], eui64_addr[4], eui64_addr[5], eui64_addr[6], eui64_addr[7] ) ;
    
    if( tagmask.t_mcu ){
    	len += sprintf(&msg.buf[len], "T_MCU=%-5.1f ",temp);
    }
    if( tagmask.v_mcu){
    	len += sprintf(&msg.buf[len], "V_MCU=%-4.2f ", v_avr);
    }
    // todo print the other data items as per their tagmasks here !!! - maneesh
    msg.buf[len++] = 0;
    packetbuf_copyfrom(&msg, len+2);

    msg.head = 1<<4; /* Version 1 */
    msg.head |= ttl;;
    msg.seqno = seqno;

    
    broadcast_send(&broadcast);
    seqno++;
    
    printf("&: %s\n", msg.buf);

    PORTE |= (1<<LED_YELLOW); 
  }
  PROCESS_END();
}

PROCESS_THREAD(init_process, ev, data)
{
  PROCESS_BEGIN();

  printf("Init\n");
  DDRE |= (1<<LED_YELLOW);
  DDRE |= (1<<LED_RED);

  //PORTE |= LED_RED; 
  //PORTE |= LED_YELLOW; 
 //PORTE &= ~LED_YELLOW; /* On */

  DDRD |= (1<<4);

  rtcc_init();

//  serial line init !!! 
//   serial_line_init();
  
// Registering the shell commands - maneesh 14/12/2013	 
#ifdef SHELL_INTERFACE 
  serial_shell_init();
  printf("serial_shell_init()\n");
  shell_register_command(&rint_command);
  printf("registered rint command\n");
  shell_register_command(&rmask_command);
  printf("registered rmask command\n"); 
#endif 

  get_eui64_addr(eui64_addr) ;
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

/*
PROCESS_THREAD(rled_process, ev, data)
{
  PROCESS_BEGIN();
  
  //printf("On") ;
  
  PROCESS_END() ;
}
*/
#endif


static struct pt send_thread;

PT_THREAD(send_message(char *txt, rimeaddr_t b, int *retval))
{
  PT_BEGIN(&send_thread);

    printf("%s \n", txt);

  *retval = 0;
  PT_END(&send_thread);
}

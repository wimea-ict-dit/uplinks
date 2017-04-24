#include "contiki.h"
#include "contiki-net.h"
#include "lib/list.h"
#include "lib/memb.h"
#include "lib/random.h"
#include "net/rime.h"
#include "rss2.h"
#include "shell.h"
#include "serial-shell.h"
#include "dev/serial-line.h"
#include <avr/sleep.h>
#include <avr/io.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAX_NEIGHBORS 64
#define SIZE          40

#define RTC_SCALE 30
#define MAX_BCAST_SIZE 99
#define DEF_TTL 0xF
uint8_t  ttl = DEF_TTL;
#define DEF_CHAN 26            // define channel number 26
#define SEQNO_EWMA_UNITY 0x100 // Moving average
#define SEQNO_EWMA_ALPHA 0x040

// declare weather parameters for sink node
double t_avr;
double v_avr;
double v_inp;
double v_va3;

// declare mac address for sink node
unsigned char eui64_addr[8] ;

//int radio_sleep = 0;
//unsigned char charbuf[SIZE];

// declare 
uint16_t h,m;

// declare 
unsigned int report_interval = 5 ;

// declare structures
// Bitwise structure for mapping the input mask
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

static struct broadcast_conn broadcast;

struct broadcast_message 
{
  uint8_t head;                    // version << 4 + ttl 
  uint8_t seqno;
  uint8_t buf[MAX_BCAST_SIZE+20];  // Check for max payload 20 extra to be able to test 
};

struct 
{
  uint16_t sent;
  uint16_t dup;
  uint16_t ignored;
  uint16_t ttl_zero;
} relay_stats;

struct neighbor 
{
  struct neighbor *next;
  rimeaddr_t addr;
  uint16_t last_rssi, last_lqi;
  uint8_t last_seqno;
  uint32_t avg_seqno_gap;         // The ->avg_gap contains the average seqno gap that we have seen from this neighbor.
};

MEMB(neighbors_memb, struct neighbor, MAX_NEIGHBORS);
LIST(neighbors_list);


// 1.declare menthod for init process
static void rtcc_init(void)
{
  TIMSK2 &=~((1<<TOIE2)|(1<<OCIE2A));  // Disable TC0 interrupt 
  ASSR |= (1<<AS2);                    // set Timer/Counter0 to be asynchronous 
  TCNT2 = 0x00;
  //TCCR2B = 0x05;                     // prescale the timer to / 128 to --> 1s 
  TCCR2B = 0x02;                       // prescale the timer to / 8   16 Hz
  while(ASSR&0x07);
  TIMSK2 |= (1<<TOIE2);  
}

// 2.declare help command method for check serial process
/*
void print_help_command_menu()
{
	printf("\n------------------Command Menu--------------------------------\n") ;
    printf("\nh\tPrints a list of supported commands\n\t Usage: h\n") ;
    printf("ri\tSets the report interval\n\t  Usage: ri <period in seconds>\n");
    printf("tagmask\tSets the report tag mask\n\t       Usage: tagmask <1 byte number representing the mask>\n");
    printf("tagmask bits correspond to the following tags MSB -> LS,\n| UT | PS | T | T_MCU | V_MCU | UP | V_IN | spare bit |\n") ;
    printf("---------------------------------------------------------------\n\n");
}
*/

// 3.declare  read sensor method for data capturng
/*
void read_sensors(void)
{
	uint16_t s;
    uint8_t p1;
  
    BATMON = 16;              //Stabilize at highest range and lowest voltage

    ADCSRB |= (1<<MUX5);      //this bit buffered till ADMUX written to!
    ADMUX = 0xc9;             //Select internal 1.6 volt ref, temperature sensor ADC channel
    ADCSRA = 0x85;            //Enable ADC, not free running, interrupt disabled, clock divider 32 (250 KHz@ 8 MHz)
    ADCSRA |= (1<<ADSC);      //Throw away conversion
  
    while 
      (ADCSRA &( 1<<ADSC) );  //Wait
    
	ADCSRA |= (1<<ADSC);      //Start conversion
    
	while 
      (ADCSRA & (1<<ADSC) );  //Wait
    
	h = ADC;
    t_avr = (double) h * 1.13 - 272.8;    
	h = 11*h-2728+(h>>2);     //Convert to celcius*10 (should be 11.3*h, approximate with 11.25*h)
    ADCSRA = 0;               //disable ADC
    ADMUX = 0;                //turn off internal vref      
    m = h/10; 
	s=h-10*m;

    for ( p1=16; p1<31; p1++) 
	{ 
      BATMON = p1;
      //  delay_us(100); 
      if ((BATMON & (1<<BATMON_OK)) == 0 ) 
        break;
    }
    v_avr = (double) 2550-75*16-75 + 75 * p1; //-75 to take the floor of the 75 mv transition window
    v_avr = v_avr/1000;
}
*/

static void broadcast_recv(struct broadcast_conn *c, const rimeaddr_t *from)
{
	struct neighbor *n;
    struct broadcast_message *msg;
    uint8_t seqno_gap;
    uint8_t relay = 1;                         // Default on 

    PORTE &= ~LED_RED;                         // set ON 
    msg = packetbuf_dataptr();

    if((msg->head >> 4) != 1 || (msg->head & 0xF) == 0) 
	{
      relay = 0;
      relay_stats.ignored++;
      goto out;
	}

    // From our own address. Can happen if we receive own pkt via relay Ignore
    if(rimeaddr_cmp(&rimeaddr_node_addr, from)) 
	{
      relay = 0;
      relay_stats.ignored++;
      goto out;
    }

    // Check if we already know this neighbor. 
    for(n = list_head(neighbors_list); n != NULL; n = list_item_next(n)) 
	{

      if(rimeaddr_cmp(&n->addr, from)) 
        break;
    }

    if(n == NULL) 
	{
      n = memb_alloc(&neighbors_memb); // New neighbor 

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
  
    n->avg_seqno_gap = (((uint32_t)seqno_gap * SEQNO_EWMA_UNITY) * SEQNO_EWMA_ALPHA) / SEQNO_EWMA_UNITY +
                        ((uint32_t)n->avg_seqno_gap * (SEQNO_EWMA_UNITY - SEQNO_EWMA_ALPHA)) / SEQNO_EWMA_UNITY;

    n->last_seqno = msg->seqno;

    printf("&: %s [ADDR=%-d.%-d SEQ=%-d TTL=%-u RSSI=%-u LQI=%-u DRP=%-d.%02d]\n",
	   msg->buf,
	   from->u8[0], from->u8[1], msg->seqno, msg->head & 0xF,
	   n->last_rssi,
	   n->last_lqi, 
	   (int)(n->avg_seqno_gap / SEQNO_EWMA_UNITY),
	   (int)(((100UL * n->avg_seqno_gap) / SEQNO_EWMA_UNITY) % 100));
	
    //PORTE |= (1<<LED_RED);	
out:
  PORTE |= LED_RED;  // turn OFF 
}

static const struct broadcast_callbacks broadcast_call = {broadcast_recv};

#ifdef SHELL_INTERFACE
PROCESS(rint_process , "Set Report Interval");
SHELL_COMMAND( rint_command, 
               "rint", 
               "rint <number> [Here the \"number\" represents the report periodicity in seconds]", 
               &rint_process) ;

PROCESS(rmask_process , "Set Report Mask");
SHELL_COMMAND( rmask_command, 
               "rmask", 
               "rmask <number> [Here the \"number\" denotes the 8 bit mask for the data fields in the Sensor Report]",
               &rmask_process) ;
               
PROCESS(rled, "Turn on led");
SHELL_COMMAND(rled_command,
              "rled",
              "rled",
              &rled_process);
#endif

// 4.declare processes
PROCESS(init_process,"init process");
//PROCESS(check_serial,"check serial");
PROCESS(broadcast_process,"broadcast process");

AUTOSTART_PROCESSES(&init_process,&broadcast_process);

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

PROCESS_THREAD(init_process,ev,data)
{
	PROCESS_BEGIN();
	
	printf("Init\n");
	DDRE |= (1<<LED_YELLOW);
	DDRE |= (1<<LED_RED);
	DDRD |= (1<<4);
	
	rtcc_init();
	
	//Registering the shell commands
#ifdef SHELL_INTERFACE 
  serial_shell_init();
  printf("serial_shell_init()\n");
  shell_register_command(&rint_command);
  printf("registered rint command\n");
  shell_register_command(&rmask_command);
  printf("registered rmask command\n"); 
#endif 
  get_eui64_addr(eui64_addr) ;        //warning - implicit declaration of function 'get_eui64_addr'
  memset(&tagmask,0x18, 1) ;          //enable t_mcu, v_mcu tags
	
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

PROCESS_THREAD(rled_process, ev, data)
{
  PROCESS_BEGIN();
  printf("On") ;
  PROCESS_END() ;
}

#endif
/*
PROCESS_THREAD(check_serial,ev,data)
{
	PROCESS_BEGIN();
	
	char delimiter[] = " ";
	char *command = NULL ;
    char *t_avr = NULL ;
    unsigned char  ri = 0 ; 
	
	printf("Process begins for check_serial !! \n") ;
	
	for(;;) 
	{
		printf("Waiting for EV = %d to be equal to  serial_line_event_message = %d\n",ev, serial_line_event_message);
        PROCESS_YIELD_UNTIL(ev == serial_line_event_message);
        printf("Serial Line Check Process Polled:  EV = %d\n",ev) ;
		
		// debug prints
		if(ev == serial_line_event_message) 
		{
			printf("received line: %s\n", (char *)data);
		} 
		else 
		{
			printf("Stil: no serial_line_event_message: received Event Number = %d\n", ev) ;
		}
		command = (char*)strtok((char*)data, (const char*)delimiter) ;		
		
		if(!strncmp(command, "h",1))
		{
			print_help_command_menu() ;
        } 
		else if(!strncmp(command,"ri",2)) 
		{
			t_avr = strtok(data, delimiter) ;
			report_interval = atoi(t_avr) ;
        } 
		else if(!strncmp(command, "tagmask",7))
		{
			t_avr = strtok(data, delimiter) ;
			ri  = atoi(t_avr[0]) ;    //warning - passing argument 1 of 'atoi' makes pointer from integer without a cast
			memcpy( (void*)&tagmask, (const void*)&ri , sizeof(ri) )  ; // the tagmask is just 8 bit long ! 
        } 
		else 
		{
			printf("Invalid command %s. Try h for a list of commands\n", command) ;
        }	// more commands can be added here
	}
	
	PROCESS_END();
}
*/
PROCESS_THREAD(broadcast_process,ev,data)
{
	static uint8_t seqno;
    struct broadcast_message msg;
    uint8_t ch, txpwr = 0;

    PROCESS_EXITHANDLER(broadcast_close(&broadcast);)
	
	PROCESS_BEGIN();
	
	broadcast_open(&broadcast, 129, &broadcast_call);
	
	// 1.get transeiver power antenna
	//rf230_set_txpower(15);
	txpwr = rf230_get_txpower();      //warning - implicit declaration of function 'rf230_get_txpower'
	
	// 2.set or allocate available channel
	//ch = rf230_get_channel();
	ch = DEF_CHAN;
    rf230_set_channel(ch);           //warning - implicit declaration of function 'rf230_set_channel'
    
	// 3.show channel and power transmission
	//printf("Channel=%-d TxPwr=%-d\n", ch, txpwr);
	printf("Channel=%-d Transeiver power(TxPwr)=%-d\n", ch, txpwr);
	
	// Just read the EUI64 address and the serial number from the at24mac chip at24mac_read(1,1,0) ;
	/*
	while(1) 
	{
		int len;
		
		//etimer_set(&et, CLOCK_SECOND * 2);
        //PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
        PROCESS_YIELD_UNTIL(ev == 0x12);

        PORTE &= ~(1<<LED_YELLOW); // Set LED On 

		// 4.read weather parameters
        //read_sensors();

        len = 0;
    
        // 5.write weather parameters and the EUI64 MAC address to the message
        len += sprintf(&msg.buf[len] , "E64=%02x%02x%02x%02x%02x%02x%02x%02x ", eui64_addr[0],eui64_addr[1], eui64_addr[2],
                                              eui64_addr[3], eui64_addr[4], eui64_addr[5], eui64_addr[6], eui64_addr[7] ) ; //warnings
											  
		if(tagmask.t_mcu)
		{
			len += sprintf(&msg.buf[len], "T_MCU=%-5.1f ", t_avr);
		}
		
        if(tagmask.v_mcu)
		{
			len += sprintf(&msg.buf[len], "V_MCU=%-4.2f ", v_avr);   //DRP=%-d.%02d
		}
        
		// 6.
        msg.buf[len++] = 0;
        packetbuf_copyfrom(&msg, len+2);

        msg.head = 1<<4;                              // Version 1 
        msg.head |= ttl;
        msg.seqno = seqno;
        
		// 7.
        broadcast_send(&broadcast);
        seqno++;
        
		printf("Channel=%-d TxPwr=%-d\n", ch, txpwr); //warning - 'txpwr' may be used uninitialized in this function
		//printf("%s and %s\n",s,t_avr);              //warning - format '%s' expects type 'char *', but argument 2 has type 'uint16_t'  
		                                              //        - format '%s' expects type 'char *', but argument 3 has type 'double'
        printf("&: %s\n", msg.buf);

        PORTE |= (1<<LED_YELLOW); 
	}
	*/
	PROCESS_END();
}
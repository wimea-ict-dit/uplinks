/*
 * Copyright (c) 2006, Technical University of Munich
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source cod e must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 *
 */
#define PRINTF(FORMAT,args...) printf_P(PSTR(FORMAT),##args)

#define ANNOUNCE_BOOT 1    //adds about 600 bytes to program size
#if ANNOUNCE_BOOT
#define PRINTA(FORMAT,args...) printf_P(PSTR(FORMAT),##args)
#else
#define PRINTA(...)
#endif

#define DEBUG 0
#if DEBUG
#define PRINTD(FORMAT,args...) printf_P(PSTR(FORMAT),##args)
#else
#define PRINTD(...)
#endif

#include <avr/pgmspace.h>
#include <avr/fuse.h>
#include <avr/eeprom.h>
#include <stdio.h>
#include <string.h>
#include <dev/watchdog.h>

#include "loader/symbols-def.h"
#include "loader/symtab.h"

#include "params.h"
#include "radio/rf230bb/rf230bb.h"
#include "net/mac/frame802154.h"
#include "net/mac/framer-802154.h"
#include "net/sicslowpan.h"

#include "contiki.h"
#include "contiki-net.h"
#include "contiki-lib.h"

#include "dev/rs232.h"
#include "dev/serial-line.h"
#include "dev/slip.h"
#include "dev/uart1.h"


#include "net/rime.h"
#include "i2cmaster.h"


/* Track interrupt flow through mac, rdc and radio driver */
#define DEBUGFLOWSIZE 32
#if DEBUGFLOWSIZE
uint8_t debugflowsize,debugflow[DEBUGFLOWSIZE];
#define DEBUGFLOW(c) if (debugflowsize<(DEBUGFLOWSIZE-1)) debugflow[debugflowsize++]=c
#else
#define DEBUGFLOW(c)
#endif

/* Get periodic prints from idle loop, from clock seconds or rtimer interrupts */
/* Use of rtimer will conflict with other rtimer interrupts such as contikimac radio cycling */
/* STAMPS will print ENERGEST outputs if that is enabled. */
#define PERIODICPRINTS 1
#if PERIODICPRINTS
//#define PINGS 64
#define ROUTES 60
#define STAMPS 60
#define STACKMONITOR 1024
uint32_t clocktime;
#define TESTRTIMER 0
#if TESTRTIMER
uint8_t rtimerflag=1;
struct rtimer rt;
void rtimercycle(void) {rtimerflag=1;}
#endif
#endif

uint16_t ledtimer;

/*-------------------------------------------------------------------------*/
/*----------------------Configuration of the .elf file---------------------*/
#if 1
/* The proper way to set the signature is */
#include <avr/signature.h>
#else
/* Older avr-gcc's may not define the needed SIGNATURE bytes. Do it manually if you get an error */
typedef struct {const unsigned char B2;const unsigned char B1;const unsigned char B0;} __signature_t;
#define SIGNATURE __signature_t __signature __attribute__((section (".signature")))
SIGNATURE = {
  .B2 = 0x01,//SIGNATURE_2, //ATMEGA128rfa1
  .B1 = 0xA7,//SIGNATURE_1, //128KB flash
  .B0 = 0x1E,//SIGNATURE_0, //Atmel
};
#endif

#if 1
/* JTAG, SPI enabled, Internal RC osc, Boot flash size 4K, 6CK+65msec delay, brownout disabled */
FUSES ={.low = 0xe2, .high = 0x99, .extended = 0xff,};
#else
/* JTAG+SPI, Boot 4096 words @ $F000, Internal  oscillator, startup 6 CK +0 ms, Brownout 1.8 volts */
FUSES ={.low = 0xC2, .high = 0x99, .extended = 0xfe,};
#endif

uint8_t
rng_get_uint8(void) {
#if 1
  /* Upper two RSSI reg bits (RND_VALUE) are random in rf231 */
  uint8_t j;
  j = (PHY_RSSI>>6) | (PHY_RSSI>>4) | (PHY_RSSI>>4) | PHY_RSSI;
#else
  /* Get a pseudo random number using the ADC */
  uint8_t i,j;
  ADCSRA=1<<ADEN;             //Enable ADC, not free running, interrupt disabled, fastest clock
  for (i=0;i<4;i++) {
    ADMUX = 0;                //toggle reference to increase noise
    ADMUX =0x1E;              //Select AREF as reference, measure 1.1 volt bandgap reference.
    ADCSRA|=1<<ADSC;          //Start conversion
    while (ADCSRA&(1<<ADSC)); //Wait till done
    j = (j<<2) + ADC;
  }
  ADCSRA=0;                   //Disable ADC
#endif
  PRINTD("rng issues %d\n",j);
  return j;
}


#define AT24MAC602_DEV_ADDR 0xB0
#define EUI64_ADDR_LOC   0x98
#define SIZE_OF_EUI64_ADDR 8


void probe_i2c_bus(){
     uint8_t ret = 0 ;
     printf("Probing I2C: \n");	
     ret =  i2c_start(AT24MAC602_DEV_ADDR+I2C_WRITE) ;  // set device address and write mode
     if( !ret ){
       printf("AT24MAC60 ") ;
       i2c_stop() ;
     }
}


/*------------Function to read EUI64 address from AT24MAC602 chip----------*/
/*-------- return true on successful read and fale on failure ---------------*/

bool get_eui64_addr(uint8_t *addr_array){

     uint8_t ret = 0 ; 	
     uint8_t i = 0 ;
    //uint8_t addr_array[SIZE_OF_EUI64_ADDR] ;

     cli() ; // clear interrupts

     i2c_init();                             // initialize I2C library

     sei() ; // set interrupts

     probe_i2c_bus() ;
     
/*
     // write 0x75 to EEPROM address 5 (Byte Write) 
     i2c_start_wait(AT24MAC602_DEV_ADDR+I2C_WRITE);     // set device address and write mode
     i2c_write(EUI_ADDR_LOCATION);                        // write address = 5
     i2c_write(0x75);                        // write value 0x75 to EEPROM
     i2c_stop();                             // set stop conditon = release bus

*/
     // read previously written value back from EEPROM address 5 
     //ret = i2c_start(AT24MAC602_DEV_ADDR+I2C_WRITE) ;     // set device address and write mode
     
     i2c_start_wait(AT24MAC602_DEV_ADDR+I2C_WRITE) ;     // set device address and write mode
     if( ret ){
	i2c_stop() ;

	printf("ERROR : No Slave Dev Found at Addr %2x\n", AT24MAC602_DEV_ADDR+I2C_WRITE) ;
	return false ;
     
     } else {	

     	if( !i2c_write(EUI64_ADDR_LOC) )                      // write address = 5
     	{
     	  if(! i2c_rep_start(AT24MAC602_DEV_ADDR+I2C_READ)){       // set device address and read mode
    
     		printf("\n********EUI64 ADDR : ") ;
     		for( i = 0 ; i < SIZE_OF_EUI64_ADDR-1 ; ++i){
			addr_array[i] = i2c_readAck() ;
			printf("%02x ",addr_array[i]) ;
    	 	}
     		addr_array[i] = i2c_readNak();                    // read one byte from EEPROM
     		printf("%02x ********\n",addr_array[i]); 
     		i2c_stop();
	  }else{
		printf("Device not found\n"); 
		return false ;
	  }
        }else{
	  printf("I2C_WRITE: Failed to write %2X\n",EUI64_ADDR_LOC) ;
	}
     }
      return true ;
} 


/*-------------------------Low level initialization------------------------*/
/*------Done in a subroutine to keep main routine stack usage small--------*/
void initialize(void)
{

  rimeaddr_t addr;
  

  rs232_init(RS232_PORT_0, USART_BAUD_38400,USART_PARITY_NONE | USART_STOP_BITS_1 | USART_DATA_BITS_8);

  UBRR0L = 25;
  UBRR0H = 0;
  UCSR0A = (1<<U2X0);  /*Double */
  UCSR0B = (1<<RXCIE0)|(1<<TXEN0)|(1<<RXEN0);	

  rs232_set_input(RS232_PORT_0 , serial_line_input_byte) ; 
 
  rs232_redirect_stdout(RS232_PORT_0);
 
  serial_line_init();

  clock_init();

  //get_eui64_addr(addr.u8) ; 

  if(MCUSR & (1<<PORF )) PRINTD("Power-on reset.\n");
  if(MCUSR & (1<<EXTRF)) PRINTD("External reset!\n");
  if(MCUSR & (1<<BORF )) PRINTD("Brownout reset!\n");
  if(MCUSR & (1<<WDRF )) PRINTD("Watchdog reset!\n");
  if(MCUSR & (1<<JTRF )) PRINTD("JTAG reset!\n");

#if STACKMONITOR
  /* Simple stack pointer highwater monitor. Checks for magic numbers in the main
   * loop. In conjuction with PERIODICPRINTS, never-used stack will be printed
   * every STACKMONITOR seconds.
   */
  {
    extern uint16_t __bss_end;
    uint16_t p=(uint16_t)&__bss_end;
    do {
      *(uint16_t *)p = 0x4242;
      p+=10;
    } while (p<SP-10); //don't overwrite our own stack
  }
#endif

  PRINTA("\n*******Booting %s*******\n",CONTIKI_VERSION_STRING);

  /* rtimers needed for radio cycling */
  rtimer_init();

  /* Initialize process subsystem */
  process_init();

  /* etimers must be started before ctimer_init */
  process_start(&etimer_process, NULL);
  ctimer_init();

  /* Start radio and radio receive process */
  NETSTACK_RADIO.init();

  /* Get a random seed for the 802.15.4 packet sequence number.
   * Some layers will ignore duplicates found in a history (e.g. Contikimac)
   * causing the initial packets to be ignored after a short-cycle restart.
   */

  random_init(rng_get_uint8());


// read mac from  at24mac602 chip over twi

/*
  if (params_get_eui64(addr.u8)) {
    PRINTA("Random EUI64 address generated\n");
  }
*/ 
  
  rimeaddr_set_node_addr(&addr); 

  printf("params_get_panid(): %d params_get_panaddr() %d", params_get_panid(), params_get_panaddr());

  /* TODO: Understand the get params functions */
  //rf230_set_pan_addr(params_get_panid(),params_get_panaddr(),(uint8_t *)&addr.u8);

  // Setting this explicitly as I do not trust what is being returned from params_get_panid() and params_get_panaddr()
  rf230_set_pan_addr(IEEE802154_PANID, 0,(uint8_t *)&addr.u8);
  rf230_set_channel(params_get_channel());
  //printf("get tx power from eeprom: %d \n", params_get_txpower());
  rf230_set_txpower(0); // Setting this explicitly as I do not trust what is being returned from params_get_txpower()

  PRINTA("EUI-64 MAC: %x-%x-%x-%x-%x-%x-%x-%x\n",addr.u8[0],addr.u8[1],addr.u8[2],addr.u8[3],addr.u8[4],addr.u8[5],addr.u8[6],addr.u8[7]);

  /* Initialize stack protocols */
  queuebuf_init();
  NETSTACK_RDC.init();
  NETSTACK_MAC.init();
  NETSTACK_NETWORK.init();

#if ANNOUNCE_BOOT
  PRINTA("%s %s, channel %u , check rate %u Hz tx power %u\n",NETSTACK_MAC.name, NETSTACK_RDC.name, rf230_get_channel(),
	 CLOCK_SECOND / (NETSTACK_RDC.channel_check_interval() == 0 ? 1:NETSTACK_RDC.channel_check_interval()),
	 rf230_get_txpower());   
#endif /* ANNOUNCE_BOOT */

/* Autostart other processes */
  autostart_start(autostart_processes);

  PRINTA("Online\n");

}

#if ROUTES && UIP_CONF_IPV6
static void
ipaddr_add(const uip_ipaddr_t *addr)
{
  uint16_t a;
  int8_t i, f;
  for(i = 0, f = 0; i < sizeof(uip_ipaddr_t); i += 2) {
    a = (addr->u8[i] << 8) + addr->u8[i + 1];
    if(a == 0 && f >= 0) {
      if(f++ == 0) PRINTF("::");
    } else {
      if(f > 0) {
        f = -1;
      } else if(i > 0) {
        PRINTF(":");
      }
      PRINTF("%x",a);
    }
  }
}
#endif

/*-------------------------------------------------------------------------*/
/*------------------------- Main Scheduler loop----------------------------*/
/*-------------------------------------------------------------------------*/
int
main(void)
{
  initialize();

  while(1) {
    process_run();
    watchdog_periodic();

    
    /* Turn off LED after a while */
    if (ledtimer) {
      if (--ledtimer==0) {
#if RF230BB_CONF_LEDONPORTE1
        PORTE&=~(1<<PE1);
#endif
#if defined(RAVEN_LCD_INTERFACE)&&0
	/* ledtimer can be set by received ping; ping the other way for testing */
	extern void raven_ping6(void);         
	raven_ping6();
#endif
      }
    }

#if 0
    /* Various entry points for debugging in the AVR Studio simulator.
     * Set as next statement and step into the routine.
     */
    NETSTACK_RADIO.send(packetbuf_hdrptr(), 42);
    process_poll(&rf230_process);
    packetbuf_clear();
    len = rf230_read(packetbuf_dataptr(), PACKETBUF_SIZE);
    packetbuf_set_datalen(42);
    NETSTACK_RDC.input();
#endif

#if 0
    /* Clock.c can trigger a periodic PLL calibration in the RF230BB driver.
     * This can show when that happens.
     */
    extern uint8_t rf230_calibrated;
    if (rf230_calibrated) {
      PRINTD("\nRF230 calibrated!\n");
      rf230_calibrated=0;
    }
#endif

    /* Set DEBUGFLOWSIZE in contiki-conf.h to track path through MAC, RDC, and RADIO */
#if DEBUGFLOWSIZE
    if (debugflowsize) {
      debugflow[debugflowsize]=0;
      PRINTF("%s",debugflow);
      debugflowsize=0;
    }
#endif

#if PERIODICPRINTS
#if TESTRTIMER
    /* Timeout can be increased up to 8 seconds maximum.
     * A one second cycle is convenient for triggering the various debug printouts.
     * The triggers are staggered to avoid printing everything at once.
     */
    if (rtimerflag) {
      rtimer_set(&rt, RTIMER_NOW()+ RTIMER_ARCH_SECOND*1UL, 1,(void *) rtimercycle, NULL);
      rtimerflag=0;
#else
      if (clocktime!=clock_seconds()) {
	clocktime=clock_seconds();
#endif

#if STAMPS
	if ((clocktime%STAMPS)==0) {
#if ENERGEST_CONF_ON
#include "lib/print-stats.h"
//	  print_stats();  commented maneesh
#elif RADIOSTATS
	  extern volatile unsigned long radioontime;
	  PRINTF("%u(%u)s\n",clocktime,radioontime);
#else
	  PRINTF("%us\n",clocktime);
#endif

	}
#endif
#if TESTRTIMER
	clocktime+=1;
#endif

#if PINGS && UIP_CONF_IPV6
	extern void raven_ping6(void); 
	if ((clocktime%PINGS)==1) {
	  PRINTF("**Ping\n");
	  raven_ping6();
	}
#endif

#if ROUTES && UIP_CONF_IPV6
	if ((clocktime%ROUTES)==2) {
      
	  extern uip_ds6_nbr_t uip_ds6_nbr_cache[];
	  extern uip_ds6_route_t uip_ds6_routing_table[];
	  extern uip_ds6_netif_t uip_ds6_if;

	  uint8_t i,j;
	  PRINTF("\nAddresses [%u max]\n",UIP_DS6_ADDR_NB);
	  for (i=0;i<UIP_DS6_ADDR_NB;i++) {
	    if (uip_ds6_if.addr_list[i].isused) {
	      ipaddr_add(&uip_ds6_if.addr_list[i].ipaddr);
	      PRINTF("\n");
	    }
	  }
	  PRINTF("\nNeighbors [%u max]\n",UIP_DS6_NBR_NB);
	  for(i = 0,j=1; i < UIP_DS6_NBR_NB; i++) {
	    if(uip_ds6_nbr_cache[i].isused) {
	      ipaddr_add(&uip_ds6_nbr_cache[i].ipaddr);
	      PRINTF("\n");
	      j=0;
	    }
	  }
	  if (j) PRINTF("  <none>");
	  PRINTF("\nRoutes [%u max]\n",UIP_DS6_ROUTE_NB);
	  {
	    uip_ds6_route_t *r;
	    PRINTF("\nRoutes [%u max]\n",UIP_DS6_ROUTE_NB);
	    j = 1;
	    for(r = uip_ds6_route_list_head();
		r != NULL;
		r = list_item_next(r)) {
	      ipaddr_add(&r->ipaddr);
	      PRINTF("/%u (via ", r->length);
	      ipaddr_add(&r->nexthop);
	      //     if(uip_ds6_routing_table[i].state.lifetime < 600) {
	      PRINTF(") %lus\n", r->state.lifetime);
	      //     } else {
	      //       PRINTF(")\n");
	      //     }
	      j = 0;
	    }
	  }
	  if (j) PRINTF("  <none>");
	  PRINTF("\n---------\n");
	}
#endif

#if STACKMONITOR
	if ((clocktime%STACKMONITOR)==3) {
	  extern uint16_t __bss_end;
	  uint16_t p=(uint16_t)&__bss_end;
	  do {
	    if (*(uint16_t *)p != 0x4242) {
	 //     PRINTF("Never-used stack > %d bytes\n",p-(uint16_t)&__bss_end);  commented maneesh
	      break;
	    }
	    p+=10;
	  } while (p<RAMEND-10);
	}
#endif

      }
#endif /* PERIODICPRINTS */

#if RF230BB&&0
      extern uint8_t rf230processflag;
      if (rf230processflag) {
	PRINTF("rf230p%d",rf230processflag);
	rf230processflag=0;
      }
#endif

#if RF230BB&&0
      extern uint8_t rf230_interrupt_flag;
      if (rf230_interrupt_flag) {
	//   if (rf230_interrupt_flag!=11) {
        PRINTF("**RI%u",rf230_interrupt_flag);
	//   }
	rf230_interrupt_flag=0;
      }
#endif
    }
    return 0;
  }

  /*---------------------------------------------------------------------------*/

  void log_message(char *m1, char *m2)
  {
    PRINTF("%s%s\n", m1, m2);
  }

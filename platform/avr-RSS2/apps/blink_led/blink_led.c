#include "contiki.h"
//#include "dev/leds.h"
#include <stdio.h>
#include <avr/io.h>
//#include "rss2.h"
#include <util/delay.h>

PROCESS(blink_led, "blink process");
AUTOSTART_PROCESSES(&blink_led);
 
PROCESS_THREAD(blink_led, ev, data)
{
  PROCESS_BEGIN();
  
                 //xxxx xxxx xxxR Yxxx 
   DDRE  = 0x18; //0000 0000 0001 1000 PORTE.3 = YELLOW and PORTE.4 = RED
   PORTE = 0x00; //0000 0000 0000 0000
 
   while(1) 
   {   
	   PORTE = 0x10;
       _delay_ms(1000);       
       //printf("Blink LED Red    ...\n");	   
       PORTE = 0x08;      
       _delay_ms(3000); 
	   //printf("Blink LED Yellow ...\n");
   }
  
  PROCESS_END();
}


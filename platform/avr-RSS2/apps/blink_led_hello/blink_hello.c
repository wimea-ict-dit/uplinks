#include "contiki.h"
#include "dev/leds.h"
//#include <util/delay.h>
#include <stdio.h>

PROCESS(hello_led_process, "Hello led process");
PROCESS(blink_led_process, "LED blink process");

AUTOSTART_PROCESSES(&hello_led_process, &blink_led_process);

// Implementation of the hello process 
PROCESS_THREAD(hello_led_process, ev, data)
{
  static struct etimer timer;
  static int count;

  PROCESS_BEGIN();
  
    etimer_set(&timer, CLOCK_CONF_SECOND * 4);
    count = 0;

    while(1) 
    {
      PROCESS_WAIT_EVENT();

      if(ev == PROCESS_EVENT_TIMER) 
	  {
        printf("Sensor node says tune... #%d\r\n", count);
        count++;

        etimer_reset(&timer);
      }
    }

  PROCESS_END();
}

// Implementation of the blink-led process 
PROCESS_THREAD(blink_led_process, ev, data)
{
  static struct etimer timer;
  static uint8_t leds_state = 0;
  
  PROCESS_BEGIN();
    DDRE  = 0x18; //0000 0000 0001 1000
    PORTE = 0x00;
  
    while(1) 
    {	  
	    etimer_set(&timer, CLOCK_CONF_SECOND * 2);
	    PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_TIMER);
	    printf("Blink Yellow LED... (state %0.2X).\r\n", leds_get());
	    PORTE = 0x10;
	  
        etimer_set(&timer, CLOCK_CONF_SECOND * 2);
	    PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_TIMER);
	    printf("Blink Red    LED... (state %0.2X).\r\n", leds_get());
	    PORTE = 0x08;
    }
  PROCESS_END();
}


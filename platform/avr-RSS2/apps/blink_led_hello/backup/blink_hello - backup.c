#include "contiki.h"
#include "dev/leds.h"
#include <util/delay.h>
#include <stdio.h>

PROCESS(hello_world_process, "Hello world process");
PROCESS(blink_led_process, "LED blink process");

AUTOSTART_PROCESSES(&hello_world_process, &blink_led_process);

// Implementation of the hello process 
PROCESS_THREAD(hello_world_process, ev, data)
{
  static struct etimer timer;
  static int count;

  PROCESS_BEGIN();

  // DDRE  = 0x18; //0000 0000 0001 1000
  // PORTE = 0x00;
  
  etimer_set(&timer, CLOCK_CONF_SECOND * 4);
  count = 0;

  while(1) {

    PROCESS_WAIT_EVENT();

    if(ev == PROCESS_EVENT_TIMER) 
	{
      printf("Sensor says no... #%d\r\n", count);
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
  
  PROCESS_BEGIN();
  
  DDRE  = 0x18; //0000 0000 0001 1000
  PORTE = 0x00;
  
  while(1) 
  {
	  //turn off for 4000ms
	PORTE = 0x00;
	_delay_ms(4000);
	
    etimer_set(&timer, CLOCK_CONF_SECOND);

    PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_TIMER);

    printf("Blink... (state %0.2X).\r\n", leds_get());
    //leds_toggle(LEDS_RED);
	
	//turn on for 2000ms
	PORTE = 0x18;
    _delay_ms(4000);          

  }
  PROCESS_END();
}


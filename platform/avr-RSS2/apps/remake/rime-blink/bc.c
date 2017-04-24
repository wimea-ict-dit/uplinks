/*
  Contiki Rime BRD demo.
  Broadcast Temp, Bat Voltage, RSSI, LQI DEMO. Robert Olsson <robert@herjulf.se>

  Heavily based on code from:

 * Copyright (c) 2007, Swedish Institute of Computer Science.
 * All rights reserved.
 *
  See Contiki for full copyright.
*/

#include "contiki.h"
#include <avr/io.h>
#include "rss2.h"
#include <util/delay.h>
 
 
PROCESS(blink_process, "blink process");
AUTOSTART_PROCESSES(&blink_process);
 
PROCESS_THREAD(blink_process, ev, data)
{
  PROCESS_BEGIN();
  DDRE   |= (1 << LED_RED);  //DDRE=0xFF;
  DDRE   |= (1 << LED_YELLOW);
  while(1) 
  {             // 500ms delay
      PORTE &= ~(1 << LED_RED);  
      PORTE |=  (1 << LED_YELLOW);
      _delay_ms(200);          
      PORTE |=  (0 << LED_RED); 
      PORTE &= ~(0 << LED_YELLOW);      
      _delay_ms(400); 
  }
  PROCESS_END();
}


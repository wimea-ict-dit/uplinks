// #include <avr/sleep.h>
// #include "contiki.h"
// #include "contiki-net.h"
// #include "lib/list.h"
// #include "lib/memb.h"
// #include "lib/random.h"
// #include "net/rime.h"
// #include <stdio.h>
// #include <string.h>
// #include <stdlib.h>
// #include "rss2.h"
// #include <avr/io.h>
// #include "shell.h"
// #include "serial-shell.h"
// #include "dev/serial-line.h"

// #define MAX_NEIGHBORS 64
// #define SIZE          40

// PROCESS(init_process,"init process");
// PROCESS(check_serial,"check serial");
// PROCESS(broadcast_process,"broadcast process");

// AUTOSTART_PROCESSES(&init_process);

// PROCESS_THREAD(init_process,ev,data)
// {
	// PROCESS_BEGIN();
	
	
	// PROCESS_END();
// }

// PROCESS_THREAD(check_serial,ev,data)
// {
	// PROCESS_BEGIN();
	
	
	// PROCESS_END();
// }

// PROCESS_THREAD(broadcast_process,ev,data)
// {
	// PROCESS_BEGIN();
	
	
	// PROCESS_END();
// }

#include "contiki.h"
#include "net/rime.h"
#include "random.h"
#include "dev/button-sensor.h"
#include "dev/leds.h"
#include <stdio.h>

#include <avr/io.h>
#include "rss2.h"
#include <util/delay.h>

static void broadcast_recv(struct broadcast_conn *c, const rimeaddr_t *from)
{
  printf("broadcast message received from %d.%d: '%s'\n", from->u8[0], from->u8[1], (char *)packetbuf_dataptr());
}
static const struct broadcast_callbacks broadcast_call = {broadcast_recv};
static struct broadcast_conn broadcast;

PROCESS(broadcast_process,"broadcast process");
AUTOSTART_PROCESSES(&broadcast_process);

PROCESS_THREAD(broadcast_process,ev,data)
{
  static struct etimer et;

  PROCESS_EXITHANDLER(broadcast_close(&broadcast);)

  PROCESS_BEGIN();

  broadcast_open(&broadcast, 129, &broadcast_call);
  
  DDRE  = 0x18; //0000 0000 0001 1000
  PORTE = 0x00; //0000 0000 0000 0000

  while(1) 
  {
    // Delay 2-4 seconds 
    etimer_set(&et, CLOCK_SECOND * 4 + random_rand() % (CLOCK_SECOND * 4));

    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    packetbuf_copyfrom("Hello", 6);
    broadcast_send(&broadcast);
    printf("broadcast message sent\n");
	
	//turn on for 200ms
	PORTE = 0x18;
    _delay_ms(200);          
    //turn off for 400ms
    PORTE = 0x00;      
    _delay_ms(400); 
  }

  PROCESS_END();
}


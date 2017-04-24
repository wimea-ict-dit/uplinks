#include "contiki.h"
#include "lib/random.h"
#include "net/rime.h"
#include "net/rime/collect.h"
#include "dev/leds.h"
#include "dev/button-sensor.h"

#include "net/netstack.h"

#include <stdio.h>

#include <avr/io.h>
#include "rss2.h"
#include <util/delay.h>

static struct collect_conn tc;

PROCESS(rime_collect_process, "rime collect process");
AUTOSTART_PROCESSES(&rime_collect_process);

static void recv(const rimeaddr_t *originator, uint8_t seqno, uint8_t hops)
{
  printf("Sink node receives message from %d.%d, seqno %d, hops %d: len %d '%s'\n",
	 originator->u8[0], originator->u8[1],
	 seqno, hops,
	 packetbuf_datalen(),
	 (char *)packetbuf_dataptr());
}

static const struct collect_callbacks callbacks = { recv };

PROCESS_THREAD(rime_collect_process, ev, data)
{
  static struct etimer periodic;
  static struct etimer et;
  
  PROCESS_BEGIN();

  DDRE  = 0x18; //0000 0000 0001 1000
  PORTE = 0x00; //0000 0000 0000 0000
  
  collect_open(&tc, 130, COLLECT_ROUTER, &callbacks);

  if(rimeaddr_node_addr.u8[0] == 1 && rimeaddr_node_addr.u8[1] == 0) 
  {
	printf("I am sink\n");
	collect_set_sink(&tc, 1);
  }

  // Allow some time for the network to settle. 
  etimer_set(&et, 120 * CLOCK_SECOND);
  PROCESS_WAIT_UNTIL(etimer_expired(&et));

  while(1) 
  {

    // Send a packet every 30 seconds. 
    if(etimer_expired(&periodic)) 
	{
      etimer_set(&periodic, CLOCK_SECOND * 30);
      etimer_set(&et, random_rand() % (CLOCK_SECOND * 30));
	  //turn on for 200ms
	  PORTE = 0x08;
      _delay_ms(30000); 
	   //turn off for 400ms
      PORTE = 0x00;      
      //_delay_ms(400); 
    }

    PROCESS_WAIT_EVENT();

    if(etimer_expired(&et)) {
      static rimeaddr_t oldparent;
      const rimeaddr_t *parent;

      printf("Send a packet\n");
      packetbuf_clear();
      packetbuf_set_datalen(sprintf(packetbuf_dataptr(),"%s", "Hello There") + 1);
      collect_send(&tc, 15);
	  
	  //turn on for 200ms
	  PORTE = 0x10;
      _delay_ms(15000); 
	   //turn off for 400ms
      PORTE = 0x00;      
      //_delay_ms(400); 

      parent = collect_parent(&tc);
	  
      if(!rimeaddr_cmp(parent, &oldparent)) 
	  {
        if(!rimeaddr_cmp(&oldparent, &rimeaddr_null)) 
		{
          printf("#L %d 0\n", oldparent.u8[0]);
		
		  //turn on for 200ms
	      PORTE = 0x18;
          _delay_ms(400); 
	      //turn off for 400ms
          PORTE = 0x00;      
          _delay_ms(200); 
        }
        if(!rimeaddr_cmp(parent, &rimeaddr_null)) 
		{
          printf("#L %d 1\n", parent->u8[0]);
		  
		  //turn on for 200ms
	      PORTE = 0x18;
          _delay_ms(200); 
	      //turn off for 400ms
          PORTE = 0x00;      
          _delay_ms(400); 
        }
        rimeaddr_copy(&oldparent, parent);
      }
    }

  }

  PROCESS_END();
}
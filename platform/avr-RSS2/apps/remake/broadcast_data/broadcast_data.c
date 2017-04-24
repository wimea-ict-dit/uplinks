#include "contiki.h"
#include "net/rime.h"
#include "random.h"
#include "dev/button-sensor.h"
#include "dev/leds.h"
#include <stdio.h>

static struct broadcast_conn broadcast;

static void broadcast_recv(struct broadcast_conn *c, const rimeaddr_t *from)
{
  printf("broadcast message received from %d.%d: '%s'\n", from->u8[0], from->u8[1], (char *)packetbuf_dataptr());
}
static const struct broadcast_callbacks broadcast_call = {
	broadcast_recv
};

PROCESS(example_broadcast_process, "Broadcast example");

AUTOSTART_PROCESSES(&example_broadcast_process);

PROCESS_THREAD(example_broadcast_process, ev, data)
{
  static struct etimer et;

  PROCESS_EXITHANDLER(broadcast_close(&broadcast);)

  PROCESS_BEGIN();

  broadcast_open(&broadcast, 129, &broadcast_call);

  while(1) {

    /* Delay 2-4 seconds */
    etimer_set(&et, CLOCK_SECOND * 4 + random_rand() % (CLOCK_SECOND * 4));

    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    packetbuf_copyfrom("Hello", 6);
    broadcast_send(&broadcast);
    printf("broadcast message sent\n");
  }

  PROCESS_END();
}



// #include "contiki.h"
// #include "net/rime.h"
// #include "lib/random.h"
// #include "net/rime/rimestats.h"
// #include "dev/leds.h"
// #include "dev/models.h"

// #define DEBUG 1
// #if DEBUG
// #include <stdio.h>
// #define PRINTF(...) printf(__VA_ARGS__)
// #else
// #define PRINTF(...)
// #endif

// #define BROADCAST_CHANNEL 129


// PROCESS(broadcast_process, "Broadcast message");
// AUTOSTART_PROCESSES(&broadcast_process);

// static void broadcast_recv(struct broadcast_conn *c, const rimeaddr_t *from)
// {
  // //leds_toggle(LEDS_RED);
  // leds_on(LEDS_YELLOW);
  // PRINTF("Broadcast message is received from %02x.%02x\n", from->u8[0],from->u8[1]);
  // PRINTF("Size=0x%02x: '0x%04x'\n", packetbuf_datalen(),*(uint16_t *)packetbuf_dataptr());
// }

// static void print_rime_stats()
// {
  // PRINTF("\nNetwork Stats\n");
  // PRINTF("   TX=%lu ,      RX=%lu\n", rimestats.tx, rimestats.rx);
  // PRINTF("LL-TX=%lu ,   LL-RX=%lu\n", rimestats.lltx, rimestats.llrx);
  // PRINTF(" Long=%lu ,   Short=%lu\n", rimestats.toolong, rimestats.tooshort);
  // PRINTF("T/Out=%lu , CCA-Err=%lu\n", rimestats.timedout, rimestats.contentiondrop);
// }

// static const struct broadcast_callbacks bc_rx = { broadcast_recv };
// static struct broadcast_conn broadcast;

// PROCESS_THREAD(broadcast_process, ev, data)
// {
  // static struct etimer et;
  // static uint16_t counter;

  // PROCESS_EXITHANDLER(broadcast_close(&broadcast);)

  // PROCESS_BEGIN();

  // PRINTF("Start\n");
  // broadcast_open(&broadcast, BROADCAST_CHANNEL, &bc_rx);
  // PRINTF("Open Broadcast Connection, channel %u\n", BROADCAST_CHANNEL);

  // while(1) {

    // // Delay 2-4 seconds 
    // etimer_set(&et, CLOCK_SECOND * 2);
    // PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
    // leds_on(LEDS_RED);
    // packetbuf_copyfrom(&counter, sizeof(counter));
    
	// PRINTF("Sending %u bytes: 0x%04x\n", packetbuf_datalen(),*(uint16_t *)packetbuf_dataptr());
    
	// if(broadcast_send(&broadcast) == 0) 
	// {
      // PRINTF("Error Sending\n");
    // }

    // print_rime_stats();
    // PRINTF("===================================\n");
    // counter++;
    // leds_off(LEDS_RED);
  // }

  // PROCESS_END();
// }


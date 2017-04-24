#include "contiki.h"
#include <stdio.h> 
#include <avr/io.h>
#include <util/delay.h>
#include <stdlib.h>
//#include <avr/interrupt.h>

# define USART_BAUDRATE 9600
# define BAUD_PRESCALE ((( F_CPU / ( USART_BAUDRATE * 16UL))) - 1)

//uint8_t mem=0;

//struct bits {
//  uint8_t b0:1, b1:1, b2:1, b3:1, b4:1, b5:1, b6:1, b7:1;
//} __attribute__((__packed__));

#define SBIT_(port,pin) ((*(volatile struct bits*)&port).b##pin)
#define SBIT(x,y) SBIT_(x,y)
#define K1_DIR SBIT( DDRC, 0 )
#define K1 SBIT( PORTC, 0 )
#define INTERUPT SBIT( mem, 0 )
#define INDEX_SMS SBIT( mem, 1 )
#define DECODE_SMS SBIT( mem, 2 )

void serial_init(){
  UCSRB = (1 << RXEN ) | (1 << TXEN );
  UCSRC = (1 << URSEL ) | (1 << UCSZ0 ) | (1 << UCSZ1 );
  UBRRH = ( BAUD_PRESCALE >> 8);
  UBRRL = BAUD_PRESCALE ;
  UCSRB |= (1 << RXCIE );
  sei ();
}

void UART_Transmit_char( unsigned char data )
{
  /* Wait for empty transmit buffer */
  while ( !( UCSRA & (1<<UDRE)) )
  ;
  /* Put data into buffer, sends the data */
  UDR = data;
}

void UART_Transmit_string( char string[] )
{
  int i=0;
  while ( string[i] > 0)
  UART_Transmit_char(string[i++]);
}

volatile char ser_rec[100]; uint8_t a=0;

void clear_sec_rec(){
  i=0; while( i<=100){ ser_rec[i]==0;i++;} i=0;a=0;
}

void gsm_init(){
  UART_Transmit_string("ATD0678130930\r");_delay_ms(200);
  //UART_Transmit_string("AT\r");_delay_ms(200);
  //UART_Transmit_string("AT\r"); _delay_ms(200);
  //UART_Transmit_string("AT\r"); _delay_ms(200);
  //send some times AT to set up the gsm
  //UART_Transmit_string("AT+CMGF=1\r"); _delay_ms(200);//set text mode
  //UART_Transmit_string("ATE0\r"); _delay_ms(200);//disable ECHO
}


PROCESS(hello_world_process, "Hello world process");
AUTOSTART_PROCESSES(&hello_world_process);

PROCESS_THREAD(hello_world_process, ev, data)
{
  PROCESS_BEGIN();
  serial_init();
  gsm_init();
  //printf("Hello, world\n");
  
  PROCESS_END();
}

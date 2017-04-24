#include <contiki.h>
#include <avr/io.h>
#include <util/delay.h>
#include <stdlib.h>
#include <avr/interrupt.h>
# define USART_BAUDRATE 9600
# define BAUD_PRESCALE ((( F_CPU / ( USART_BAUDRATE * 16UL))) - 1)

uint8_t mem=0;

struct bits {
  uint8_t b0:1, b1:1, b2:1, b3:1, b4:1, b5:1, b6:1, b7:1;
} __attribute__((__packed__));

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
  UART_Transmit_string("AT\r");_delay_ms(200);
  UART_Transmit_string("AT\r"); _delay_ms(200);
  UART_Transmit_string("AT\r"); _delay_ms(200);
  //send some times AT to set up the gsm
  UART_Transmit_string("AT+CMGF=1\r"); _delay_ms(200);//set text mode
  UART_Transmit_string("ATE0\r"); _delay_ms(200);//disable ECHO
}

void gsm_send_AVRFREAK(){
  UART_Transmit_string("AT+CMGS=");
  UART_Transmit_char(34);
  UART_Transmit_string("xxxxxx");//the phone number
  UART_Transmit_char(34);
  UART_Transmit_string("\r");
  _delay_ms(3000);
  UART_Transmit_string("AVRFREAK\r");_delay_ms(3000);
  UART_Transmit_char(0x1A);
  _delay_ms(3000);
}

char sms_index1(){
  i=0;
  while( i<=50)
  {
    i++; //check if gsm sends SM",
    if(ser_rec[i]==((char) 'S'))
    {
      i++; 
      if(ser_rec[i]==((char) 'M'))
      {
        i++; 
        if(ser_rec[i]==((char) '"'))
        { 
          i++;
          if(ser_rec[i]==((char) ','))
          {
            i++; 
            return sms_index; 
            i=51; M1=1; 
          }
        }
      }
    }
  }//returns it
  i=0; a=0;
  if(M1==0){return 0;} M1=0; // if not returns 0
  clear_sec_rec();
}

void sms_decode()
{
  i=0;
  _delay_ms(3000);
  while( i<=100)
  {
    i++;
    if(ser_rec[i]==((char) 'K'))
    {
      i++;
      if(ser_rec[i]==((char) '1'))
      {
        i++;
        if(ser_rec[i]==((char) '='))
        {
          i++;
          if(ser_rec[i]==((char) '1'))
          {
            i++;
            K1=1;
          }
        }
      }
    }
  }//activates the port
  i=0;
  clear_sec_rec();
  _delay_ms(200);//clears the buffer
  if(sms_index!=0)//delete the sms
  {
    UART_Transmit_string("AT+CMGD=");
    UART_Transmit_char(sms_index);
    UART_Transmit_string("\r");
  }
}

int main(void)
{
  serial_init();//inits the serial port
  gsm_init();//init gsm
  K1_DIR=1;//sets portc.0 as output
  while(1)
  {
    if((INTERUPT==1)&&(DECODE_SMS==0))
    {
      _delay_ms(3000);
      //if there is interupt get the sms_index
      sms_index=sms_index1();
      INTERUPT=0;
      if(sms_index!=0)
      {
        INDEX_SMS=1;
      }
    }
    //and activate the next part of code
    if((INTERUPT==1)&&(DECODE_SMS==1))//decodes the sms
    {sms_decode(); DECODE_SMS=0; INTERUPT=0;}
  }
}

ISR ( USART_RXC_vect )
{
  ser_rec[a]=UDR;
  if((ser_rec[a]!=0xd)&&(ser_rec[a]!=0xa))
  {//we do not need theese bytes 0xd,0xa
    a++;
    INTERUPT=1;
  }
}

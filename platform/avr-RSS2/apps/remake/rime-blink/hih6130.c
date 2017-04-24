#define HIH6130_ADDR 0x27
#define START_CM     0xA0
#define START_NOM    0x80


int read_hih6130(void)
{
  unsigned char hh, hl, th, tl, status;
  unsigned char addr = (HIH6130_ADDR<<1);
  uint16_t u16_rh, u16_t_rh;
  int i;

    /* Turn on in case pwr is off */
  PORTE |= HUM_PWR;

  if( ! (mode & M_HUM_PWR_ALWAYS)) 
    for(i = 0; i < 80; i++)
      delay_us(500); 

     status = i2c_start(addr+I2C_WRITE);  /* Measurement request */

     if(status) {
       err.rh_miss++;
       return 1;
     }

     for(i = 0; i < 80; i++)
       delay_us(500); /* 40 ms. Typ  36.65 for rh & temp*/

     i2c_start(addr+I2C_READ);  

     /* rh */

     hh = i2c_read(1);
     status = (hh >> 6) & 0x03;

     if(status == 1) 
       err.rh_stale_data++;
     if(status > 1)
       err.rh_misc++;

     hh = hh & 0x3f;
     hl = i2c_read(1);

     /* temp */

     th = i2c_read(1);
     tl = i2c_read(0);

     i2c_stop();

     if( ! (mode & M_HUM_PWR_ALWAYS)) 
       PORTE &= ~HUM_PWR;
     
     u16_rh = (uint16_t) (hh<<8) + hl;
     u16_t_rh = ((uint16_t) (th<<8) + tl) >> 2;

     rh = (double) u16_rh * 6.104e-3;
     t_rh = (double) u16_t_rh * 1.007e-2 - 40.0;

     return 0;
}

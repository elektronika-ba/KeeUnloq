#include <avr/io.h>

#include "i2c.h"

//###############################
// TWI - I2C related functions //
//###############################
// twi init
void twi_init(uint8_t speed_khz)
{
	// set i2c for desired SCL speed
	TWSR = 0;									// I2C prescaler value
	// TWBR = 0.5((F_CPU/SCL) - 16)
	TWBR = speed_khz;	// set I2C clock speed
	return;
}

// start i2c
void twi_start(uint8_t addr)
{
  TWCR = ( 1<<TWEN ) | ( 1<<TWINT ) | ( 1<<TWSTA );

  while(!(TWCR & ( 1<<TWINT )));

  TWDR = addr;
  TWCR = ( 1<<TWEN ) | ( 1<<TWINT );

  while(!(TWCR & ( 1<<TWINT ))); 						// wait

  return;
}
// send a byte over i2c
void twi_tx_byte(uint8_t data)
{
  TWDR = data;
  TWCR = ( 1<<TWEN ) | ( 1<<TWINT );

  while(!(TWCR & ( 1<<TWINT ))); 						// wait

  return;
}
// stop i2c
void twi_stop(void)
{
  TWCR = ( 1<<TWEN ) | ( 1<<TWINT ) | ( 1<<TWSTO );

  return;
}
// receive byte + ack
uint8_t twi_rx_ack(void)
{
  TWCR = ( 1<<TWEN ) | ( 1<<TWINT ) | ( 1<<TWEA );

  while(!(TWCR & ( 1<<TWINT )));						// wait

  return TWDR;
}
// receive byte + nack
uint8_t twi_rx_nack(void)
{
  TWCR = ( 1<<TWEN ) | ( 1<< TWINT );

  while(!(TWCR & ( 1<<TWINT )));						// wait

  return TWDR;
}

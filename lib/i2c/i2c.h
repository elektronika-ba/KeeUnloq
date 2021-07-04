#ifndef I2C_H_
#define I2C_H_

#include <stdio.h>

// TWI - I2C related functions
void twi_init(uint8_t speed);
void twi_start(uint8_t addr); // start i2c
void twi_tx_byte(uint8_t data); // send byte
void twi_stop(void); // stop i2x
uint8_t twi_rx_ack(void); // receive ACK
uint8_t twi_rx_nack(void); // receive NACK

#endif /*I2C_H_*/

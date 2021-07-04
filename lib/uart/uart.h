/*
 * uart.h
 *
 * Created: 26. 1. 2021. 10:00:42
 *  Author: Trax
 */ 

#ifndef UART_H_
#define UART_H_

#include <stdio.h>

// Calculate UBRR value for baud rate "bau"

void uart_init(uint8_t baudrate);
void uart_putc(char);
void uart_puts(char *);
void uart_putsn(char *, char);
//char uart_getc(uint16_t);
//void uart_getsn(char *, uint8_t, uint16_t);

#endif /* UART_H_ */
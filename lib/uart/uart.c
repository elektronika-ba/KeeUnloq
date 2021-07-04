/*
 * uart.c
 *
 * Created: 26. 1. 2021. 10:00:35
 *  Author: Trax
 */ 

#include <avr/io.h>

#include "uart.h"

// init the UART registers
void uart_init(uint8_t baudrate)
{
	UBRR0 = baudrate;
	UCSR0C = _BV(UCSZ01) | _BV(UCSZ00);	// character size 8 bit
	UCSR0B = _BV(RXEN0) | _BV(TXEN0);	// receiver and transmitter enabled

	return;
}

// transmit a character over UART
void uart_putc(char data)
{
	while(!(UCSR0A & (1 << UDRE0)));	// wait until TX ready
	UDR0 = data;

	return;
}

// transmit a null-terminated string over UART
void uart_puts(char *s)
{
	while (*s)
	{
		uart_putc(*s);
		s++;
	}

	return;
}

// transmit n characters of a given string over UART
void uart_putsn(char *s, char n)
{
	while (n>0)
	{
		uart_putc(*s);
		s++;
		n--;
	}

	return;
}

/*
// receive a char from UART - +waiting for it!
// parameter: -1 - wait until received
// 			  else - wait this much [ms] and break with error bit set if byte not received
char uart_getc(uint16_t timeout_ms)
{
	// set the timeout, used or not
	uart_timeout = timeout_ms;

	bs(bUART_RX,BSYS); // initially say that the byte is received

	// wait until byte is received OR while not timed-out
	while(!(UCSR0A & (1 << RXC0)))
	{
		// if timer expired in the meantime while used
		if(!uart_timeout && timeout_ms)
		{
			bc(bUART_RX,BSYS); // invalid byte
			break; // timeout!
		}
	}

	return UDR0;
}
void uart_getsn(char *buff, uint8_t n, uint16_t timeout)
{
	while(n>0)
	{
		*buff=uart_getc(timeout);
		if( !bv(bUART_RX,BSYS) ) return; // terminate the loop if timed out
		buff++;
		n--;
	}

	return;
}
*/

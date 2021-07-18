/*
 * main.h
 *
 * Created: 26. 1. 2021. 09:56:45
 *  Author: Trax
 */ 

#ifndef MAIN_H_
#define MAIN_H_

#include "lib/uart/uart.h"
#include "lib/i2c/i2c.h"

#include "keeloq.h"
#include "keeloq_decode.h"
#include "keeloq_prog.h"
#include "ee_db.h"
#include "ee_db_record.h"

#define DEBUG 1

// Limitations: ATmega328P (and family) PORTB.4 can only be used as INPUT if SPI mode is enabled.

#ifndef F_CPU
	#error You must define CPU frequency (F_CPU) in AVR Studio (Project Properties -> Toolchain -> Symbols >> add F_CPU=16000000UL) or your Makefile
#endif

#ifndef _BV
	#define _BV(n) (1 << n)
#endif

#define	calc_UBRR(bau)			(F_CPU/16/bau-1)
#define calc_TWI(khz)			(((F_CPU/khz) - 16)/2)

#define nop() asm volatile("nop")

// EEPROM addresses
#define	EEPROM_START				0								// start of our data in eeprom is here
#define EEPROM_MAGIC				EEPROM_START + 0				// eeprom OK - magic value
#define EEPROM_OPTION_STATES		EEPROM_MAGIC + 1				// state of operating options
#define EEPROM_MASTER_CRYPT_KEY		EEPROM_OPTION_STATES + 1		// master crypt key for learning encrypted HCS devices via RF

#define EEPROM_MAGIC_VALUE			0xAA

// Device options state
#define OP_STATE_1		0b00000001	// Receiver module
#define OP_STATE_2		0b00000010	// MITM upgrader
#define OP_STATE_3		0b00000100	// Grab, collect
#define OP_STATE_4		0b00001000	// Remote emulator from memory
#define OP_STATE_LEN	4			// 4 options currently implemented

// RF IN data pin
#define	RX_PIN			2
#define	RX_DDR			DDRB
#define	RX_PINREG		PINB
#define	RX_PORT			PORTB
#define	RX_PCINTBIT		PCINT2
#define	RX_PCMSKREG		PCMSK0
#define	RX_PCICRBIT		PCIE0

// RF OUT data pin PORTB.1 (OC1A pin is data output)
#define	TX_PIN			1
#define	TX_DDR			DDRB
#define	TX_PORT			PORTB

// OPTOCOUPLERs outputs S0..S3
#define	S0_PIN			2
#define	S0_DDR			DDRC
#define	S0_PORT			PORTC

#define	S1_PIN			3
#define	S1_DDR			DDRC
#define	S1_PORT			PORTC

#define	S2_PIN			1
#define	S2_DDR			DDRC
#define	S2_PORT			PORTC

#define	S3_PIN			0
#define	S3_DDR			DDRC
#define	S3_PORT			PORTC

// BUTTONs inputs
#define	BTNS0_PIN			2
#define	BTNS0_DDR			DDRD
#define	BTNS0_PINREG		PIND
#define	BTNS0_PORT			PORTD
#define	BTNS0_PCINTBIT		PCINT18
#define	BTNS0_PCMSKREG		PCMSK2
#define	BTNS0_PCICRBIT		PCIE2

#define	BTNS1_PIN			3
#define	BTNS1_DDR			DDRD
#define	BTNS1_PINREG		PIND
#define	BTNS1_PORT			PORTD
#define	BTNS1_PCINTBIT		PCINT19
#define	BTNS1_PCMSKREG		PCMSK2
#define	BTNS1_PCICRBIT		PCIE2

#define	BTNS2_PIN			4
#define	BTNS2_DDR			DDRD
#define	BTNS2_PINREG		PIND
#define	BTNS2_PORT			PORTD
#define	BTNS2_PCINTBIT		PCINT20
#define	BTNS2_PCMSKREG		PCMSK2
#define	BTNS2_PCICRBIT		PCIE2

#define	BTNS3_PIN			5
#define	BTNS3_DDR			DDRD
#define	BTNS3_PINREG		PIND
#define	BTNS3_PORT			PORTD
#define	BTNS3_PCINTBIT		PCINT21
#define	BTNS3_PCMSKREG		PCMSK2
#define	BTNS3_PCICRBIT		PCIE2

// for prev_btn & btn_hold global variables
#define BTNS0_MASK			0b00000001
#define BTNS1_MASK			0b00000010
#define BTNS2_MASK			0b00000100
#define BTNS3_MASK			0b00001000

// Button related timers
#define	BTN_HOLD_TMR						950		// miliseconds to pronounce button as held rather than pressed
#define BTN_MODE_CHANGE_EXPECTER			15000	// ms to exit the mode-change.. mode
#define BTN_MODE_CHANGE_CURR_REPORTER		2000	// ms, on every this much report current mode of operation
#define BTN_PROG_N_ENROLL_EXPECTER			20000	// ms to expect button press for programming the encoder IC
#define BTN_REMOVE_REMOTE_EXPECTER			10000	// ms to expect remote reception via RF and to remove it
#define BTN_ENROLL_FIRST_REMOTE_EXPECTER	10000	// ms to expect FIRST remote reception via RF and to enroll it
#define BTN_ENROLL_SECOND_REMOTE_EXPECTER	5000	// ms to expect SECOND remote reception via RF and to enroll it
#define BTN_CLEAR_MEMORY_EXPECTER			5000	// ms to expect second button hold for entire memory to be cleared

// LEDs
#define	LEDA_PIN		0
#define	LEDA_DDR		DDRB
#define	LEDA_PORT		PORTB

#define	LEDB_PIN		7
#define	LEDB_DDR		DDRD
#define	LEDB_PORT		PORTD

#define	LEDC_PIN		6
#define	LEDC_DDR		DDRD
#define	LEDC_PORT		PORTD

// HCS PROGRAMMER PINS
// DATA
#define	HCS_PROG_DATA_PIN		3
#define	HCS_PROG_DATA_DDR		DDRB
#define	HCS_PROG_DATA_PINREG	PINB
#define	HCS_PROG_DATA_PORT		PORTB
// S0&S1 pins
#define	HCS_PROG_S0S1_PIN		4
#define	HCS_PROG_S0S1_DDR		DDRB
#define	HCS_PROG_S0S1_PINREG	PINB
#define	HCS_PROG_S0S1_PORT		PORTB
// S2/CLK pin
#define	HCS_PROG_S2CLK_PIN		5
#define	HCS_PROG_S2CLK_DDR		DDRB
#define	HCS_PROG_S2CLK_PINREG	PINB
#define	HCS_PROG_S2CLK_PORT		PORTB
// S3 pin
#define	HCS_PROG_S3_PIN			1
#define	HCS_PROG_S3_DDR			DDRB
#define	HCS_PROG_S3_PINREG		PINB
#define	HCS_PROG_S3_PORT		PORTB

// Code macros
#define setInput(ddr, pin)		( (ddr) &= (uint8_t)~_BV(pin) )
#define setOutput(ddr, pin)		( (ddr) |= (uint8_t)_BV(pin) )

#define set0(port, pin)			( (port) &= (uint8_t)~_BV(pin) )
#define setLow(port, pin)		( (port) &= (uint8_t)~_BV(pin) )

#define set1(port, pin)			( (port) |= (uint8_t)_BV(pin) )
#define setHigh(port, pin)		( (port) |= (uint8_t)_BV(pin) )

#define togglePin(port, pin)	( (port) ^= (uint8_t)_BV(pin) )

// Boolean macros
#define	BSYS					0										// bank index[0] name.. used for system work
#define	BAPP1					1										// bank index[1] name.. used for application work
#define BOOL_BANK_SIZE			2										// how many banks (bytes)
#define bv(bit,bank) 			( bools[bank] & (uint8_t)_BV(bit) ) 	// boolean values GET/READ macro: if(bv(3,BSYS))...
#define	bs(bit,bank) 			( bools[bank] |= (uint8_t)_BV(bit) ) 	// boolean values SET macro: bs(5,BSYS)
#define	bc(bit,bank) 			( bools[bank] &= (uint8_t)~_BV(bit) ) 	// boolean values CLEAR macro: bc(4,BAPP1)
#define	bt(bit,bank) 			( bools[bank] ^= (uint8_t)_BV(bit) ) 	// boolean values TOGGLE macro: bt(6,BAPP1)

// Custom boolean bit/flags, 0..7 in bank 1 - SYSTEM SPECIFIC
#define	bUART_RX				0	// received byte is valid (1) or not (0)

// masks for the ISR LED blinker
#define ISR_LED_A_MASK			0b00000001
#define ISR_LED_B_MASK			0b00000010
#define ISR_LED_C_MASK			0b00000100

#define ISR_LED_BLINK_XFAST_MS	100
#define ISR_LED_BLINK_FAST_MS	150
#define ISR_LED_BLINK_NORMAL_MS	400
#define ISR_LED_BLINK_SLOW_MS	850

// misc stuff
uint8_t next_within_window(uint16_t, uint16_t, uint16_t);
void clear_pending_buttons();
uint8_t handle_ui_buttons();
void misc_hw_init();
void set_mode(uint8_t, uint8_t);
void update_settings_to_eeprom();
void delay_ms_(uint64_t);
void show_number_on_leds(uint16_t);
void handle_tx_emulator_buttons();
void delay_builtin_ms_(uint16_t);

uint8_t event_keydown(struct KEELOQ_DECODE_PLAIN *, struct eedb_record_header *, struct eedb_hcs_record *, uint8_t *);
void event_keyup(struct KEELOQ_DECODE_PLAIN *, struct eedb_record_header *, struct eedb_hcs_record *);

// LED helpers
void leda_on();
void leda_off();
void leda_blink(uint8_t);
void ledb_on();
void ledb_off();
void ledb_blink(uint8_t);
void ledc_on();
void ledc_off();
void ledc_blink(uint8_t);
void led_isrblink(uint8_t, uint16_t);

// main app stuff
uint8_t prog_n_enroll_66bit_hcs200(struct KEELOQ_DECODE_PROG_PROFILE *);
uint8_t prog_n_enroll_66bit_hcs201(struct KEELOQ_DECODE_PROG_PROFILE *);
uint8_t prog_n_enroll_66bit_hcs300_301_320(struct KEELOQ_DECODE_PROG_PROFILE *);
uint8_t prog_n_enroll_67bit_hcs360_361(struct KEELOQ_DECODE_PROG_PROFILE *);
uint8_t prog_hcs_encoder(struct KEELOQ_DECODE_PROG_PROFILE *);
void enroll_transmitter_rf();
void remove_transmitter_rf();
void clear_all_memory();

// hardware callbacks for keeloq library
void keeloq_rx_init_hw();
void keeloq_rx_deinit_hw();
void keeloq_init_tx_hw();
void keeloq_deinit_tx_hw();
void keeloq_pin_tx_hw(uint8_t);

// hardware callbacks for keeloq programmer library
void keeloq_prog_init_hw(uint8_t);
void keeloq_prog_set_clk_pin_hw(uint8_t);
void keeloq_prog_set_data_pin_hw(uint8_t);
uint8_t keeloq_prog_get_data_pin_hw();
void keeloq_prog_deinit_hw();

#endif /* MAIN_H_ */
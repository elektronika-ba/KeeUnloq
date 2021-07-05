/*
 * main.c
 *
 * Created: 26. 1. 2021.
 * Author : Trax
 */

#include <stdio.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <stdlib.h>
#include <avr/eeprom.h>
#include <string.h>
#include <math.h>
#include <util/delay.h>

#include "main.h"

// ticker timer
volatile uint64_t milliseconds = 0;
volatile uint64_t delay_milliseconds = 0;

// ISR led blinked related
volatile uint8_t isr_led_blinker = 0;
volatile uint16_t isr_led_blinker_tmr = 0;
volatile uint16_t isr_led_blinker_rate = ISR_LED_BLINK_NORMAL_MS;

// button related
volatile uint8_t prev_btn = 0; // previous button states for interrupt handling
volatile uint8_t btn_press = 0; // also for the buttons with the isr
volatile uint8_t btn_hold = 0; // also for the buttons with the isr
volatile uint16_t btn_hold_timer = 0; // to detect button holds
volatile uint16_t btn_expect_timer = 0; // to detect idling of button press

volatile uint64_t master_crypt_key = 0; // LOADED FROM EEPROM upon startup

// database tables
volatile struct eedb_ctx eedb_hcsmitm;
volatile struct eedb_ctx eedb_hcsdb;
volatile struct eedb_ctx eedb_hcslogdevices;
volatile struct eedb_ctx eedb_hcsloglogs;

// misc
volatile uint16_t action_expecter_timer = 0;

// KeeLoq context
volatile struct keeloq_ctx kl_ctx;

// misc working variables
volatile uint8_t op_mode; // current device operating mode (stored in internal eeprom)
volatile uint16_t last_grabbed_eeaddr;

void foreach_hcs_loglog_record_callback(volatile struct eedb_ctx *ctx, struct eedb_record_header *header, void *record) {
	uart_puts("    foreach_hcs_loglog_record_callback()\r\n");
	
	struct eedb_log_record *log_record = record;
	
	uart_puts("    ");
	char tmp[64];
	for(uint8_t i=0; i<KL_BUFF_LEN; i++) {
		sprintf(tmp, "0x%02X ", log_record->kl_rx_buff[i]);
		uart_puts(tmp);
	}
	uart_puts("\r\n");
}

void foreach_hcs_logdevice_record_callback(volatile struct eedb_ctx *ctx, struct eedb_record_header *header, void *record) {
	uart_puts("foreach_hcs_logdevice_record_callback()\r\n");
	
	struct eedb_hcs_record *hcs_record = record;
	
	char tmp[64];
	sprintf(tmp, "ENCODER: %u, %lu\r\n", hcs_record->encoder, hcs_record->serialno);
	uart_puts(tmp);
	
	// pokupi child recorde ovog klinca
	struct eedb_log_record one_log_record;
	eedb_for_each_record(&eedb_hcsloglogs, 0, hcs_record->serialno, &foreach_hcs_loglog_record_callback, 0, (void *)&one_log_record);
	
	uart_puts("\r\n");
}

int main(void)
{
	// Misc hardware init
	// this turns off all outputs & leds
	misc_hw_init();

	// UART init
	setInput(DDRD, 0);
	setOutput(DDRD, 1);
	uart_init(calc_UBRR(250000));

	// I2C init
	setInput(DDRC, 4); // SDA data pin input
	setOutput(PORTC, 4); // turn ON internal pullup
	setInput(DDRC, 5); // SCL clock pin input
	setOutput(PORTC, 5); // turn ON internal pullup
	twi_init(calc_TWI(400000)); // Hz

	// Initialize Timer0 overflow ISR for 1ms interval
	TCCR0A = 0;
	TCCR0B = _BV(CS01) | _BV(CS00); // 1:64 prescaled, timer started! at 16MHz this will overflow at 1.024ms
	TIMSK0 |= _BV(TOIE0); // for ISR(TIMER0_OVF_vect)

	// Interrupts ON
	// Note: global interrupts should NOT be disabled, everything is relying on them from now on!
	sei();

	delay_ms_(50);

	// Setup the context for KeeLoq TX&RX library
	kl_ctx.fn_rx_init_hw = &keeloq_rx_init_hw;
	kl_ctx.fn_rx_deinit_hw = &keeloq_rx_deinit_hw;
	kl_ctx.fn_tx_init_hw = &keeloq_init_tx_hw;
	kl_ctx.fn_tx_deinit_hw = &keeloq_deinit_tx_hw;
	kl_ctx.fn_tx_pin_hw = &keeloq_pin_tx_hw;
	// init it
	kl_init_ctx(&kl_ctx);
	
	// read settings from internal EEPROM
	// eeprom has some settings?
    if( eeprom_read_byte((uint8_t *)EEPROM_MAGIC) == 0xAA ) { // 0xAA is my "valid flag"
		uart_puts("EEPROM VALID.\r\n");
		
		op_mode = eeprom_read_byte((uint8_t *)EEPROM_OP_MODE);
		
		eeprom_read_block((uint64_t *)&master_crypt_key, (uint8_t *)EEPROM_MASTER_CRYPT_KEY, 8);

		// todo: ostalo...
	}
	// nope, use defaults
	else {
		uart_puts("EEPROM INVALID.\r\n");
		
		op_mode = OP_MODE_1;

		master_crypt_key = 0xDFD209D119A813CE; // some random value for development
		
		// todo: ostalo...
		
		// prebaci to odma u EEPROM, nastavicemo odatle ubuduce
		update_settings_to_eeprom();
	}

	char tmp[128];
	sprintf(tmp, "OP-MODE: %u\r\n", op_mode);
	uart_puts(tmp);
	sprintf(tmp, "CRYPT KEY: 0x%04X%04X%04X%04X\r\n", (uint16_t)(master_crypt_key >> 48), (uint16_t)(master_crypt_key >> 32), (uint16_t)(master_crypt_key >> 16), (uint16_t)master_crypt_key);
	uart_puts(tmp);

	// change mode on startup?
	// check to see if button S0 is pressed upon startup
	if( !(BTNS0_PINREG & _BV(BTNS0_PIN)) ) {
		leda_on();

		delay_ms_(500);

		ledb_blink(op_mode + 1); // blink current mode

		uint8_t op_mode_new = op_mode;

		// expect S0 button press now, and iterate mode of operation. when 5s expire after last S1 press, exit mode-change procedure
		btn_expect_timer = BTN_MODE_CHANGE_EXPECTER;
		clear_pending_buttons(); // clear any pending button press or hold
		milliseconds = 0; // use ticker to periodically report current mode of operation
		while(btn_expect_timer) {

			if(btn_press & BTNS0_MASK) {
				btn_press &= ~BTNS0_MASK; // clear press
				btn_expect_timer = BTN_MODE_CHANGE_EXPECTER; // reload

				op_mode_new++;
				if(op_mode_new > OP_MODE_LAST) {
					op_mode_new = OP_MODE_1;
				}

				milliseconds = BTN_MODE_CHANGE_CURR_REPORTER; // report now!
			}

			if (milliseconds >= BTN_MODE_CHANGE_CURR_REPORTER) {
				milliseconds = 0;
				ledb_blink(op_mode_new + 1);

				// prevent changing the mode during blinking, we are blind during that period
				// clear_pending_buttons(); // clear any pending button press or hold
			}
		}

		// op-mode changed?
		if(op_mode_new != op_mode)
		{
			op_mode = op_mode_new;

			// save new mode to eeprom
			update_settings_to_eeprom();
		}

		leda_off();
	}

	// initialize EEPROM database tables
	ledb_on();

	// TABLE: HCS101 device identity for the MITM mode
	eedb_hcsmitm.start_eeaddr = 0;
	eedb_hcsmitm.record_capacity = 1;
	eedb_hcsmitm.sizeof_record_entry = sizeof(struct eedb_hcs_record);
	eedb_hcsmitm.i2c_addr = 0b10100000;
	eedb_hcsmitm.fn_i2c_start = &twi_start;
	eedb_hcsmitm.fn_i2c_stop = &twi_stop;
	eedb_hcsmitm.fn_i2c_rx_ack = &twi_rx_ack;
	eedb_hcsmitm.fn_i2c_rx_nack = &twi_rx_nack;
	eedb_hcsmitm.fn_i2c_tx = &twi_tx_byte;
	eedb_init_ctx(&eedb_hcsmitm);

	// TABLE: HCS device identities
	eedb_hcsdb.start_eeaddr = eedb_hcsmitm._next_free_eeaddr; // start where previous table ended
	eedb_hcsdb.record_capacity = 250;
	eedb_hcsdb.sizeof_record_entry = sizeof(struct eedb_hcs_record);
	eedb_hcsdb.i2c_addr = 0b10100000;
	eedb_hcsdb.fn_i2c_start = &twi_start;
	eedb_hcsdb.fn_i2c_stop = &twi_stop;
	eedb_hcsdb.fn_i2c_rx_ack = &twi_rx_ack;
	eedb_hcsdb.fn_i2c_rx_nack = &twi_rx_nack;
	eedb_hcsdb.fn_i2c_tx = &twi_tx_byte;
	eedb_init_ctx(&eedb_hcsdb);

	// TABLE: HCS sniffing log HCS devices
	eedb_hcslogdevices.start_eeaddr = eedb_hcsdb._next_free_eeaddr; // start where previous table ended
	eedb_hcslogdevices.record_capacity = 100;
	eedb_hcslogdevices.sizeof_record_entry = sizeof(struct eedb_hcs_record);
	eedb_hcslogdevices.i2c_addr = 0b10100000;
	eedb_hcslogdevices.fn_i2c_start = &twi_start;
	eedb_hcslogdevices.fn_i2c_stop = &twi_stop;
	eedb_hcslogdevices.fn_i2c_rx_ack = &twi_rx_ack;
	eedb_hcslogdevices.fn_i2c_rx_nack = &twi_rx_nack;
	eedb_hcslogdevices.fn_i2c_tx = &twi_tx_byte;
	eedb_init_ctx(&eedb_hcslogdevices);

	// TABLE: HCS sniffing log HCS devices LOGs
	// THIS IS A CHILD TABLE OF "eedb_hcslogdevices"
	eedb_hcsloglogs.start_eeaddr = eedb_hcslogdevices._next_free_eeaddr; // start where previous table ended
	eedb_hcsloglogs.record_capacity = 500;
	eedb_hcsloglogs.sizeof_record_entry = sizeof(struct eedb_log_record);
	eedb_hcsloglogs.i2c_addr = 0b10100000;
	eedb_hcsloglogs.fn_i2c_start = &twi_start;
	eedb_hcsloglogs.fn_i2c_stop = &twi_stop;
	eedb_hcsloglogs.fn_i2c_rx_ack = &twi_rx_ack;
	eedb_hcsloglogs.fn_i2c_rx_nack = &twi_rx_nack;
	eedb_hcsloglogs.fn_i2c_tx = &twi_tx_byte;
	eedb_init_ctx(&eedb_hcsloglogs);

	ledb_off();
	
	// DEBUGGING: print all grabbed hcs devices
	struct eedb_hcs_record one_record;
	eedb_for_each_record(&eedb_hcslogdevices, EEDB_PKFK_ANY, 0, &foreach_hcs_logdevice_record_callback, 0, (void *)&one_record);
	
	// report operating mode to LED
	leda_blink(op_mode+1);

	uart_puts("RESUME>\r\nEND>\r\n");

	// start KeeLoq decoder if not in remote transmitter emulator
	if (op_mode != OP_MODE_4) {
		kl_rx_stop(&kl_ctx);
		kl_rx_start(&kl_ctx); // start the keeloq rx
	}

	// for looping and processing
	uint8_t processed = 0;
	uint8_t decode_ok = 0;
	uint8_t verify_ok = 0;
	struct KEELOQ_DECODE_PLAIN decoded;
	struct eedb_record_header header;
	struct eedb_hcs_record record;

	clear_pending_buttons(); // clear any pending button press or hold
	while(1)
	{
		// in op-modes 1 & 2 expect buttons for programming
		// programming also de-initializes KeeLoq decoder library so we need to re-init it here
		if(op_mode == OP_MODE_1 || op_mode == OP_MODE_2 || op_mode == OP_MODE_3) {
			uint8_t need_to_reinit_kl_rx = check_remote_prog_entry();

			// re-start KeeLoq decoder because there was some programming done and hardware *might need* to be re-initialized
			if(need_to_reinit_kl_rx) {
				need_to_reinit_kl_rx = 0;

				kl_rx_stop(&kl_ctx);
				kl_rx_start(&kl_ctx); // start the keeloq rx
			}
		}
		
		// remote transmitter emulator
		if (op_mode == OP_MODE_3 || op_mode == OP_MODE_4) {
			// TODO
			//check_tx_emulator_buttons();
		}

		char tmp[64];

		// turn LED A on while button is being pressed on a remote
		if(kl_ctx.kl_rx_rf_act == KL_RF_ACT_BUSY) {
			leda_on();			
		}

		// KeeLoq library received something
		if (kl_ctx.kl_rx_buff_state == KL_BUFF_FULL) {
			// perform processing, just once
			if (!processed) {
				processed = 1;
				
				sprintf(tmp, "Header length: [%u] us\r\n", kl_ctx.kl_rx_header_length);
				uart_puts(tmp);
				sprintf(tmp, "Timing element: [%u] us\r\n", kl_ctx.kl_rx_timing_element);
				uart_puts(tmp);

				memset(&decoded, 0, sizeof(struct KEELOQ_DECODE_PLAIN));
				memset(&header, 0, sizeof(struct eedb_record_header));
				memset(&record, 0, sizeof(struct eedb_hcs_record));

				uart_puts("RX!\r\n");

				decode_ok = keeloq_decode((uint8_t *)kl_ctx.kl_rx_buff, kl_ctx.kl_rx_buff_bit_index, 0, &decoded);
				// decoding is OK?
				if (decode_ok) {
					uart_puts("DECODE OK\r\n");

					sprintf(tmp, "SERIAL: %lu\r\n", decoded.serial);
					uart_puts(tmp);

					verify_ok = event_keydown(&decoded, &header, &record, (uint8_t *)kl_ctx.kl_rx_buff);
				}
				else {
					uart_puts("DECODE ERROR\r\n");
				}
			}
			// each other time if it is still presed, call it again but with a flag
			else if (processed) {
				// decoding & verified was OK? just PROCESS it immediatelly
				if (decode_ok && verify_ok) {
					process_keydown(&decoded, &header, &record, 1, (uint8_t *)kl_ctx.kl_rx_buff); // PROCESS AGAIN but with a repeated-flag
				}
			}
		}

		// transmission processed, and finally has ended (i.e. button released on the remote)
		if (kl_ctx.kl_rx_rf_act == KL_RF_ACT_IDLE) {
			leda_off();
			
			// if it was processed, it is safe to call keyup event
			if(processed) {
				kl_rx_flush(&kl_ctx); // "flush" buffer, make room for next code to be pushed into the RX buffer

				uart_puts("RX STOP.\r\n\r\n");

				// decoding was OK?
				if (decode_ok) {
					event_keyup(&decoded, &header, &record);
				}

				processed = 0;
			}
		}
	}
}

// key pressed on a remote, lets see if it is a valid one
uint8_t event_keydown(struct KEELOQ_DECODE_PLAIN *decoded, struct eedb_record_header *header, struct eedb_hcs_record *record, uint8_t *kl_rx_buff) {
	switch (op_mode) {
		// MODE: KeeLoq standard receiver
		// MODE: MITM Upgrader
		case OP_MODE_1:
		case OP_MODE_2:
		{
			// verify in database
			uint16_t eeaddr = eedb_find_record_eeaddr(&eedb_hcsdb, decoded->serial, 0, 0);
			if (eeaddr != EEDB_INVALID_ADDR) {
				eedb_read_record_by_eeaddr(&eedb_hcsdb, eeaddr, header, record);

				// fixed-code
				if (record->encoder == ENCODER_HCS101) {
					process_keydown(decoded, header, record, 0, kl_rx_buff);
				}
				// rolling-code
				else {
					// re-decode but now with a proper key
					uint8_t decode_ok = keeloq_decode((uint8_t *)kl_ctx.kl_rx_buff, kl_ctx.kl_rx_buff_bit_index, record->crypt_key, decoded);
					if (decode_ok) {
						// discrimination must match with database value
						// buttons vs buttons-encrypted must match
						// counter value must be within allowed window
						if (
							decoded->disc == record->discriminator
							&& decoded->buttons == decoded->buttons_enc
							&& next_within_window(decoded->counter, record->counter, 16)
							) {

							process_keydown(decoded, header, record, 0, kl_rx_buff);

							// update database with new COUNTER value received
							record->counter = decoded->counter;
							record->counter_resync = decoded->counter; // follow
							eedb_update_record(&eedb_hcsdb, header->pk, 0, 0, 0, record);
						}
						else {
							// try re-syncing within a DOUBLE OPERATION larger window of 32K
							if (next_within_window(decoded->counter, record->counter, 32767)) {
								// but this must be a successive transmission (window of 1)
								if (next_within_window(decoded->counter, record->counter_resync, 1)) {
									record->counter = decoded->counter;
								}

								// update database with new COUNTER value received but in the RESYNC field
								record->counter_resync = decoded->counter;
								eedb_update_record(&eedb_hcsdb, header->pk, 0, 0, 0, record);
							}
						}
					}
					else {
						return 0; // err
					}
				}
			}
			else {
				return 0; // err
			}
		}
		break;

		// MODE: Grabber/Logger
		case OP_MODE_3:
			// no further processing required, go to process_keydown()
			process_keydown(decoded, header, record, 0, kl_rx_buff);
		break;

		// MODE: Remote transmitter emulator
		case OP_MODE_4:
			// we expect nothing here my friend
		break;
	}

	return 1; // ok
}

// key released on a remote
void event_keyup(struct KEELOQ_DECODE_PLAIN *decoded, struct eedb_record_header *header, struct eedb_hcs_record *record) {
	// no special event on keyup, just call process_keyup()
	process_keyup(decoded, header, record);
}

void process_keydown(struct KEELOQ_DECODE_PLAIN *decoded, struct eedb_record_header *header, struct eedb_hcs_record *record, uint8_t repeated, uint8_t *kl_rx_buff) {
	switch (op_mode) {
	case OP_MODE_1:
		// do this only first time
		if (!repeated) {
			if (decoded->buttons & 1) {
				setHigh(S0_PORT, S0_PIN);
			}
			if (decoded->buttons & 2) {
				setHigh(S1_PORT, S1_PIN);
			}
			if (decoded->buttons & 4) {
				setHigh(S2_PORT, S2_PIN);
			}
			if (decoded->buttons & 8) {
				setHigh(S3_PORT, S3_PIN);
			}
		}
		break;

	case OP_MODE_2:
		{
			// ucitaj iz eeproma MITM HCS101 profil
			struct eedb_hcs_record hcs101record;
			uint16_t eeaddr = eedb_find_record_eeaddr(&eedb_hcsmitm, EEDB_PKFK_ANY, 0, 0);
			if (eeaddr != EEDB_INVALID_ADDR) {
				ledb_on();
				
				kl_rx_stop(&kl_ctx);
				
				eedb_read_record_by_eeaddr(&eedb_hcsmitm, eeaddr, 0, &hcs101record);

				// transfer from edb_hcs_record to KEELOQ_DECODE_PLAIN so we can encode it and transmit
				struct KEELOQ_DECODE_PLAIN hcs101decoded;
				hcs101decoded.buttons = hcs101record.buttons;
				hcs101decoded.counter = ++hcs101record.counter; // increment counter value in the record
				hcs101decoded.serial = hcs101record.serialno;
				hcs101decoded.serial3 = hcs101record.serial3no;
				hcs101decoded.vlow = 0; // our voltage is never low

				// encode as HCS101 (meaning encode it without using a key)
				char hcs101buff[KL_BUFF_LEN];
				keeloq_encode(ENCODER_HCS101, &hcs101decoded, 0, (uint8_t *)&hcs101buff);

				// send a burst few times, just in case receiver is lazy
				for(uint8_t i = 0; i < 3; i++) {
					kl_tx(&kl_ctx, (uint8_t *)&hcs101buff, 66, hcs101record.timing_element, 23, hcs101record.header_length, 15000);
				}

				// update HCS101 MITM profile, the counter value has changed above (++hcs101record.counter)
				eedb_update_record(&eedb_hcsmitm, EEDB_PKFK_ANY, 0, 0, 0, &hcs101record);
				
				delay_ms_(50);
				
				kl_rx_start(&kl_ctx);
				//kl_ctx.kl_rx_state = KL_RX_FULL_DONE; // messy
				
				ledb_off();		
			}
		
		}
		break;

	case OP_MODE_3:
		if (!repeated) {
			ledb_on();

			char tmp[72];
			sprintf(tmp, "LOGGING SERIAL: %lu\r\n", decoded->serial);
			uart_puts(tmp);
			
			// vidi imal ga vec u grabbed tabeli, ako nema insertuj, ako ima probaj ga klasificirati i updejtuj record
			last_grabbed_eeaddr = eedb_find_record_eeaddr(&eedb_hcslogdevices, decoded->serial, 0, 0);
			struct eedb_hcs_record dbrecord;
			if (last_grabbed_eeaddr == EEDB_INVALID_ADDR) {
				uart_puts("NOT FOUND\r\n");
				
				dbrecord.encoder = ENCODER_UNKNOWN;
				dbrecord.crypt_key = 0; // we dont know this
				dbrecord.counter = decoded->counter; // for rolling codes we dont know this
				dbrecord.discriminator = decoded->disc; // for rolling codes we dont know this
				dbrecord.serialno = decoded->serial; // we always know this
				dbrecord.serial3no = decoded->serial3; // this is only in case this was HCS101 we just received, but we don't know up front
				// information needed for possible re-transmission later
				dbrecord.buttons = decoded->buttons; // we always know this
				dbrecord.timing_element = kl_ctx.kl_rx_timing_element;
				dbrecord.header_length = kl_ctx.kl_rx_header_length;

				// save to database
				eedb_insert_record(&eedb_hcslogdevices, decoded->serial, 0, &dbrecord);
			}
			else {
				// ucitaj prethodnu prvi transmisiju, i ako vec nije - klasificiraj ga kao HCS101 ili neki od rolling-code-ova
				eedb_read_record_by_eeaddr(&eedb_hcslogdevices, last_grabbed_eeaddr, 0, &dbrecord);

				sprintf(tmp, "FOUND AS: %u, SERIAL: %lu\r\n", dbrecord.encoder, dbrecord.serialno);
				uart_puts(tmp);

				// lets try to classify it if not already classified
				if(dbrecord.encoder == ENCODER_UNKNOWN) {
					// our best guess that this is HCS101 fixed encoder
					if(kl_ctx.kl_rx_buff_bit_index == 66
						&& decoded->disc == dbrecord.discriminator
						&& decoded->serial3 == dbrecord.serial3no
						&& decoded->buttons == decoded->buttons_enc)
					{
						dbrecord.encoder = ENCODER_HCS101;
					}
					else if(kl_ctx.kl_rx_buff_bit_index == 66) {
						dbrecord.encoder = ENCODER_HCS200; // it could be this one
					}
					else if(kl_ctx.kl_rx_buff_bit_index == 67) {
						dbrecord.encoder = ENCODER_HCS360; // it could be this one
					}
					else if(kl_ctx.kl_rx_buff_bit_index == 69) {
						dbrecord.encoder = ENCODER_HCS362; // it could be this one
					}

					// update record in database, if we figured out which one it could be
					if(dbrecord.encoder != ENCODER_UNKNOWN) {
						sprintf(tmp, "CLASSIFIED AS: %u\r\n", dbrecord.encoder);
						uart_puts(tmp);
						eedb_update_record(&eedb_hcslogdevices, decoded->serial, 0, 0, 0, &dbrecord);
					}
				}
			}

			// snimi i log entry ako nije HCS101, jer njega nemamo sta snimati, samo se buttonsi mijenjaju
			if(dbrecord.encoder != ENCODER_HCS101) {
				struct eedb_log_record log_record;
				memcpy(log_record.kl_rx_buff, kl_rx_buff, KL_BUFF_LEN);
				
				// save to database
				// note to myself: i should make PK auto increment functionality for this reason...
				eedb_insert_record(&eedb_hcsloglogs, 0, decoded->serial, &log_record);
			}
			
			ledb_off();
		}
		break;

	case OP_MODE_4:
		{
			// todo...		
		}
		break;
	}
}

void process_keyup(struct KEELOQ_DECODE_PLAIN *decoded, struct eedb_record_header *header, struct eedb_hcs_record *record) {
	switch (op_mode) {
		// MODE: KeeLoq standard receiver
		case OP_MODE_1:
			setLow(S0_PORT, S0_PIN);
			setLow(S1_PORT, S1_PIN);
			setLow(S2_PORT, S2_PIN);
			setLow(S3_PORT, S3_PIN);
			break;

		// MODE: MITM Upgrader
		case OP_MODE_2:

			break;

		// MODE: Grabber/Logger
		case OP_MODE_3:

			break;

		// MODE: Remote transmitter emulator
		case OP_MODE_4:

			break;
	}
}

void clear_pending_buttons() {
	btn_hold = 0;
	btn_press = 0;
}

uint8_t check_remote_prog_entry() {
	uint8_t was_prog_at_all = 0;
	
	// Program & learn (S0 hold)
	if(btn_hold & BTNS0_MASK) {
		clear_pending_buttons(); // clear any pending button press or hold
		
		ledb_on();

		uint8_t was_prog, prog_result;
		struct KEELOQ_DECODE_PROG_PROFILE prog_profile;

		// within next 15 seconds, expect buttons S0-S2 to be pressed in order to program&enrol HCS encoder IC
		btn_expect_timer = BTN_PROG_N_ENROLL_EXPECTER;
		while(btn_expect_timer) {
			was_prog = 0;
			
			// S0
			if(btn_press & BTNS0_MASK) {
				was_prog = 1;
				prog_result = prog_n_enroll_66bit_hcs200(&prog_profile);
				btn_expect_timer = BTN_PROG_N_ENROLL_EXPECTER; // reload
				clear_pending_buttons(); // clear any pending button press or hold
			}
			// S1
			else if(btn_press & BTNS1_MASK) {
				was_prog = 1;
				prog_result = prog_n_enroll_66bit_hcs201(&prog_profile);
				btn_expect_timer = BTN_PROG_N_ENROLL_EXPECTER; // reload
				clear_pending_buttons(); // clear any pending button press or hold
			}
			// S2
			else if(btn_press & BTNS2_MASK) {
				was_prog = 1;
				prog_result = prog_n_enroll_66bit_hcs300_301_320(&prog_profile);
				btn_expect_timer = BTN_PROG_N_ENROLL_EXPECTER; // reload
				clear_pending_buttons(); // clear any pending button press or hold
			}
			// S3
			else if(btn_press & BTNS3_MASK) {
				was_prog = 1;
				prog_result = prog_n_enroll_67bit_hcs360_361(&prog_profile);
				btn_expect_timer = BTN_PROG_N_ENROLL_EXPECTER; // reload
				clear_pending_buttons(); // clear any pending button press or hold
			}
			
			if(was_prog) {
				was_prog_at_all = 1;
				if(prog_result) {
					// create database entry to save it
					struct eedb_hcs_record record;
					record.counter = prog_profile.sync;
					record.crypt_key = prog_profile.key;
					record.discriminator = prog_profile.disc_hcs201;
					record.encoder = prog_profile.encoder;
					record.serialno = prog_profile.ser;
					
					// save to database
					eedb_upsert_record(&eedb_hcsdb, prog_profile.ser, 0, 0, &record);

					ledc_blink(3);
				}
				else {
					leda_blink(5);
				}
				was_prog = 0;
			}
		}
		
		ledb_off();
		clear_pending_buttons(); // clear any pending button press or hold
	}
	
	// Learn from RF (S1 hold)
	if(btn_hold & BTNS1_MASK) {
		clear_pending_buttons(); // clear any pending button press or hold
		
		enroll_transmitter_rf();
				
		clear_pending_buttons(); // clear any pending button press or hold

		was_prog_at_all = 1;
	}
	
	// Remove single transmitter from memory (S2 hold)
	if(btn_hold & BTNS2_MASK) {
		clear_pending_buttons(); // clear any pending button press or hold
		
		// blink LED B during entire process
		led_isrblink(ISR_LED_B_MASK, ISR_LED_BLINK_FAST_MS);

		remove_transmitter_rf();

		// done. turn blinker off and also LED if it remained ON
		led_isrblink(ISR_LED_B_MASK, 0);
		ledb_off();
		clear_pending_buttons(); // clear any pending button press or hold
		
		was_prog_at_all = 1;
	}
	
	// Clear all memory (S3 hold)
	if(btn_hold & BTNS3_MASK) {
		clear_pending_buttons(); // clear any pending button press or hold

		// blink LED A during entire process
		led_isrblink(ISR_LED_A_MASK, ISR_LED_BLINK_FAST_MS);

		// within next 5 sec user needs to hold button S2 now
		btn_expect_timer = BTN_CLEAR_MEMORY_EXPECTER;
		while(btn_expect_timer) {
			if(btn_hold & BTNS2_MASK) {
				clear_pending_buttons(); // clear any pending button press or hold

				ledb_on();
				ledc_on();
				
				clear_all_memory();
				
				ledb_off();
				ledc_off();
				btn_expect_timer = 0; // get out, we are done
			}
		}

		// done. turn blinker off and also LED if it remained ON
		led_isrblink(ISR_LED_A_MASK, 0);
		leda_off();
		clear_pending_buttons(); // clear any pending button press or hold
	}
	
	return was_prog_at_all;
}

void enroll_transmitter_rf() {
	kl_rx_stop(&kl_ctx);
	kl_rx_start(&kl_ctx); // start the keeloq rx
	
	// for remembering what we received in the first go
	uint8_t first_rx_done = 0;
	uint8_t first_rx_kl_buff[KL_BUFF_LEN];

	ledb_on();

	struct KEELOQ_DECODE_PLAIN *decoded;
	struct KEELOQ_DECODE_PLAIN decoded_rolling1;
	struct KEELOQ_DECODE_PLAIN decoded_fixed1;
	
	// 10 seconds window for receive 2x -> match -> enroll/update
	action_expecter_timer = BTN_ENROLL_FIRST_REMOTE_EXPECTER;
	while(action_expecter_timer) {
		
		// turn LED A on while button is pressed on a remote
		if(kl_ctx.kl_rx_rf_act == KL_RF_ACT_BUSY) {
			leda_on();
		}
		
		// receive a remote via RF, transmission has ended
		if(kl_ctx.kl_rx_rf_act == KL_RF_ACT_IDLE && kl_ctx.kl_rx_buff_state == KL_BUFF_FULL) {
			leda_off();
			
			kl_rx_stop(&kl_ctx); // stop keeloq rx

			// second reception?
			if(first_rx_done) {
				led_isrblink(ISR_LED_B_MASK, 0); // stop blinking
				ledb_on(); // indicate second reception by turning it ON constantly

				// decode first transmission using master key
				// decode second transmission using master key
				// if serial numbers do not match: cancel programming of this remote, else continue.
				// serial number already exists in eeprom memory: cancel programming of this remote, else continue.
				// it decoded successfully using master key?
				//		yes: enroll into memory
				//		no: decode both without master key and see if it is HCS101

				keeloq_decode(first_rx_kl_buff, kl_ctx.kl_rx_buff_bit_index, master_crypt_key, &decoded_rolling1);
				struct KEELOQ_DECODE_PLAIN decoded_rolling2;
				keeloq_decode((uint8_t *)kl_ctx.kl_rx_buff, kl_ctx.kl_rx_buff_bit_index, master_crypt_key, &decoded_rolling2);

				uint8_t encoder = ENCODER_INVALID;

				// make sure someone isn't using two remotes during this process
				// serial numbers match -> continue
				if(decoded_rolling1.serial == decoded_rolling2.serial) {
					// classify the remote,
					// if it is one of the encrypted series:
					// discrimination bits must match
					// fixed portion and rolling-code button information must match too between any transmission and successive transmissions as well
					// counter from the second transmission must by > then first transmission within window of say 5 transmissions
					if (
						decoded_rolling1.disc == decoded_rolling2.disc
						&& decoded_rolling1.buttons == decoded_rolling1.buttons_enc && decoded_rolling2.buttons == decoded_rolling2.buttons_enc
						&& decoded_rolling1.buttons == decoded_rolling2.buttons
						&& next_within_window(decoded_rolling2.counter, decoded_rolling1.counter, 5)
						) {
						// it is one of the encrypted ones, lets figure out which one

						// if 66 bit:
						//		we can assume it is hcs200...
						// else if 67 bit:
						//		it is one of hcs360/361
						// else if 69 bit:
						//		it is hcs362
						// else: unsupported device

						// HCS362
						if (kl_ctx.kl_rx_buff_bit_index == 69) {
							encoder = ENCODER_HCS362;
						}
						// HCS360/361
						else if (kl_ctx.kl_rx_buff_bit_index == 67) {
							encoder = ENCODER_HCS360; // assume it is HCS360
						}
						// HCS101, HCS200, HCS201, HCS300, HCS301, HCS320
						else if (kl_ctx.kl_rx_buff_bit_index == 66) {
							// we can only assume it is HCS200
							encoder = ENCODER_HCS200;
						}
						decoded = &decoded_rolling1;
					}
					// the decryption with masterkey failed
					// maybe it is fixed-code encoder HCS101?
					else if (kl_ctx.kl_rx_buff_bit_index == 66) {
						// decode both transmissions without the key this time and compare them to see if this was HCS101
						keeloq_decode(first_rx_kl_buff, kl_ctx.kl_rx_buff_bit_index, 0, &decoded_fixed1);
						struct KEELOQ_DECODE_PLAIN decoded_fixed2;
						keeloq_decode((uint8_t*)kl_ctx.kl_rx_buff, kl_ctx.kl_rx_buff_bit_index, 0, &decoded_fixed2);

						// HCS101 also increases its counter value so we can apply the same logic here
						if (
							decoded_fixed1.buttons == decoded_fixed1.buttons_enc && decoded_fixed2.buttons == decoded_fixed2.buttons_enc
							&& decoded_fixed1.buttons == decoded_fixed2.buttons
							&& next_within_window(decoded_fixed2.counter, decoded_fixed1.counter, 5)
							) {
							encoder = ENCODER_HCS101;
							decoded = &decoded_fixed1;
						}
					}
				}

				// encoder classified? let's save it into eeprom memorajz
				if(encoder != ENCODER_INVALID) {
					// create database entry to save it
					struct eedb_hcs_record record;
					record.encoder = encoder;
					record.crypt_key = master_crypt_key;
					record.counter = decoded->counter;
					record.discriminator = decoded->disc;
					record.serialno = decoded->serial;
					record.serial3no = decoded->serial3;
					// information needed for possible re-transmission later
					record.buttons = decoded->buttons;
					record.timing_element = kl_ctx.kl_rx_timing_element;
					record.header_length = kl_ctx.kl_rx_header_length;

					// MODE: MITM Upgrader & HCS101 received? - store it in special section
					if (op_mode == OP_MODE_2 && encoder == ENCODER_HCS101) {
						// save to database
						uint16_t inserted = eedb_insert_record(&eedb_hcsmitm, record.serialno, 0, &record);
						if(inserted == EEDB_INVALID_ADDR) {
							leda_blink(3); // report ERROR - there is already HCS101 memorised at the location. user needs to perform CLEAR MEMORY
						}
						else {
							ledc_blink(5); // report OK - memorized as HCS101 - unsecure device
						}
					}
					// MODE: KeeLoq standard receiver
					// MODE: MITM Upgrader
					else {
						// already exists in memory?
						uint16_t eeaddr = eedb_find_record_eeaddr(&eedb_hcsdb, decoded->serial, 0, 0);

						// it is found in database, cancel programming
						if (eeaddr != EEDB_INVALID_ADDR) {
							delay_ms_(500);
							leda_blink(2); // report error
						}
						// store to eeprom
						else {
							// save to database
							eedb_insert_record(&eedb_hcsdb, record.serialno, 0, &record);

							// report to LED
							if (encoder == ENCODER_HCS101) {
								ledc_blink(5); // report OK - memorized as HCS101 - unsecure device
							}
							else {
								ledc_blink(2); // report OK - successfully decryption
							}
						}
					}
				}
				// encoder not supported or different serial numbers received
				else {
					delay_ms_(500);
					leda_blink(5); // report error					
				}

				first_rx_done = 0; // clear this
				action_expecter_timer = BTN_ENROLL_FIRST_REMOTE_EXPECTER; // reload for next remote
			}
			// nope, this was first reception
			else {
				memcpy(first_rx_kl_buff, (uint8_t *)kl_ctx.kl_rx_buff, KL_BUFF_LEN); // remember the received buffer
				first_rx_done = 1;
				led_isrblink(ISR_LED_B_MASK, ISR_LED_BLINK_XFAST_MS); // indicate first reception by blinking it
				action_expecter_timer = BTN_ENROLL_SECOND_REMOTE_EXPECTER; // reload to expect next transmission
			}
			
			kl_rx_start(&kl_ctx); // re-start the keeloq rx
		}
	}
	
	led_isrblink(ISR_LED_B_MASK, 0); // stop blinking if reception of second remote has expired
	ledb_off();
	
	kl_rx_stop(&kl_ctx); // stop keeloq rx
}

uint8_t next_within_window(uint16_t next, uint16_t baseline, uint16_t window) {
	// no overflow of window
	if(baseline + window > baseline) {
		// NEXT(BASELINE .. BASELINE + WINDOW)
		if(next > baseline && next <= (baseline + window)) {
			return 1;
		}
	}
	// window overflows
	else {
		// upper window NEXT(BASELINE .. MAX)
		if(next > baseline && next <= 0xFFFF) {
			return 1;
		}
		// lower window NEXT(0 .. BASELINE + WINDOW that overflowen)
		else {
			if(next <= baseline + window) {
				return 1;
			}
		}
	}
	return 0;
}

void remove_transmitter_rf() {
	kl_rx_stop(&kl_ctx);
	kl_rx_start(&kl_ctx); // start the keeloq rx

	// 10 seconds window for receive -> match -> delete
	action_expecter_timer = BTN_REMOVE_REMOTE_EXPECTER;
	while(action_expecter_timer) {

		// turn LED A on while button is pressed on a remote
		if(kl_ctx.kl_rx_rf_act == KL_RF_ACT_BUSY) {
			leda_on();
		}

		// receive a remote via RF
		if(kl_ctx.kl_rx_rf_act == KL_RF_ACT_IDLE && kl_ctx.kl_rx_buff_state == KL_BUFF_FULL) {
			leda_off();
			
			kl_rx_stop(&kl_ctx); // stop keeloq rx
			
			// we only need to extract the serial number from this reception
			
			struct KEELOQ_DECODE_PLAIN decoded;
			keeloq_decode((uint8_t *)kl_ctx.kl_rx_buff, kl_ctx.kl_rx_buff_bit_index, 0, &decoded);

			// delete record from eeprom memory via PK: decoded.serial
			uint8_t deleted = eedb_delete_record(&eedb_hcsdb, decoded.serial, 0, 0);

			// all good?
			if(deleted) {
				ledc_blink(2);
			}
			else {
				delay_ms_(700); // nije pronadjen u memoriji
				leda_blink(2);
			}
			
			action_expecter_timer = BTN_REMOVE_REMOTE_EXPECTER; // reload

			kl_rx_start(&kl_ctx); // re-start the keeloq rx
		}
	}

	kl_rx_stop(&kl_ctx); // stop keeloq rx
}

// clear corresponding memory depending on current operating mode
void clear_all_memory() {
	switch (op_mode) {
		// MODE: KeeLoq standard receiver
		case OP_MODE_1:
			// delete only memorised HCS devices that can operate us
			eedb_format_memory(&eedb_hcsdb);
		break;
		// MODE: MITM Upgrader
		case OP_MODE_2:
			// delete only MITM emulation device
			eedb_format_memory(&eedb_hcsmitm);
		break;
		// Grabber/logger
		case OP_MODE_3:
			eedb_format_memory(&eedb_hcslogdevices);
			eedb_format_memory(&eedb_hcsloglogs);
		break;
		
		case OP_MODE_4:
			// todo?
		break;
	}
}

uint8_t prog_n_enroll_66bit_hcs200(struct KEELOQ_DECODE_PROG_PROFILE *prog_profile) {
	// create HCS chip programming profile
	prog_profile->encoder = ENCODER_HCS200;
	srandom(milliseconds);
	prog_profile->key = ((uint64_t)random() << 32) | random();
	delay_ms_(random() % 100); // randomize serial number with "unknown" seed
	srandom(milliseconds);
	prog_profile->ser = random();
	prog_profile->sync = (uint16_t)random() & 0x00FF; // keep in the 0-255 range
	prog_profile->config = _BV(HCS200_CONFIG_VLOW) | (uint8_t)random();

	// program!
	return prog_hcs_encoder(prog_profile);
}

uint8_t prog_n_enroll_66bit_hcs201(struct KEELOQ_DECODE_PROG_PROFILE *prog_profile) {
	// create HCS chip programming profile
	prog_profile->encoder = ENCODER_HCS201;
	srandom(milliseconds);
	prog_profile->key = ((uint64_t)random() << 32) | random();
	delay_ms_(random() % 100); // randomize serial number with "unknown" seed
	srandom(milliseconds);
	prog_profile->ser = random();
	prog_profile->sync = (uint16_t)random() & 0x00FF; // keep in the 0-255 range
	prog_profile->disc_hcs201 = (uint16_t)rand();
	prog_profile->config = 0x0000;

	// program!
	return prog_hcs_encoder(prog_profile);
}

uint8_t prog_n_enroll_66bit_hcs300_301_320(struct KEELOQ_DECODE_PROG_PROFILE *prog_profile) {
	// create HCS chip programming profile
	prog_profile->encoder = ENCODER_HCS300; // we assume it is HCS300
	srandom(milliseconds);
	prog_profile->key = ((uint64_t)random() << 32) | random();
	delay_ms_(random() % 100); // randomize serial number with "unknown" seed
	srandom(milliseconds);
	prog_profile->ser = random();
	prog_profile->sync = (uint16_t)random() & 0x00FF; // keep in the 0-255 range
	prog_profile->config = (_BV(HCS300_301_320_CONFIG_VLOW) | _BV(HCS300_301_320_CONFIG_OVR_0) | _BV(HCS300_301_320_CONFIG_OVR_1)) | (uint8_t)random();
	
	// program!
	return prog_hcs_encoder(prog_profile);
}

uint8_t prog_n_enroll_67bit_hcs360_361(struct KEELOQ_DECODE_PROG_PROFILE *prog_profile) {
	// create HCS chip programming profile
	prog_profile->encoder = ENCODER_HCS360; // we assume it is HCS360
	srandom(milliseconds);
	prog_profile->key = ((uint64_t)random() << 32) | random();
	delay_ms_(random() % 100); // randomize serial number with "unknown" seed
	srandom(milliseconds);
	prog_profile->ser = random();
	prog_profile->sync = (uint16_t)random() & 0x00FF; // keep in the 0-255 range
	prog_profile->config = 0x0000;
	
	// program!
	return prog_hcs_encoder(prog_profile);
}

uint8_t prog_hcs_encoder(struct KEELOQ_DECODE_PROG_PROFILE *prog_profile) {
	struct keeloq_prog_ctx prog_ctx;
	prog_ctx.fn_get_data_pin_hw = &keeloq_prog_get_data_pin_hw;
	prog_ctx.fn_prog_deinit_hw = &keeloq_prog_deinit_hw;
	prog_ctx.fn_prog_init_hw = &keeloq_prog_init_hw;
	prog_ctx.fn_set_clk_pin_hw = &keeloq_prog_set_clk_pin_hw;
	prog_ctx.fn_set_data_pin_hw = &keeloq_prog_set_data_pin_hw;
	kl_prog_init_ctx(&prog_ctx);

	// build the stream
	uint8_t prog_stream[24];
	keeloq_decode_build_prog_stream(prog_stream, prog_profile);
		
	// program and verify the hcs200 chip
	return kl_prog(&prog_ctx, prog_stream, 192, 1);
}

void update_settings_to_eeprom() {
	// save all working stuff to eeprom, and mark if VALID

	// OP-MODE
	eeprom_write_byte((uint8_t *)EEPROM_OP_MODE, op_mode);

	// MASTER CRYPT-KEY
	eeprom_write_block((uint64_t *)&master_crypt_key, (uint8_t *)EEPROM_MASTER_CRYPT_KEY, 8);

	// say eeprom is valid
	eeprom_write_byte((uint8_t *)EEPROM_MAGIC, 0xAA);
}

void misc_hw_init() {
	// Buttons S0..S3
	BTNS0_DDR &= ~_BV(BTNS0_PIN); 						// pin is input
	BTNS0_PORT |= _BV(BTNS0_PIN); 						// turn ON internal pullup
	BTNS0_PCMSKREG |= _BV(BTNS0_PCINTBIT); 				// set (un-mask) PCINTn pin for interrupt on change
	PCICR |= _BV(BTNS0_PCICRBIT); 						// enable wanted PCICR

	BTNS1_DDR &= ~_BV(BTNS1_PIN); 						// pin is input
	BTNS1_PORT |= _BV(BTNS1_PIN); 						// turn ON internal pullup
	BTNS1_PCMSKREG |= _BV(BTNS1_PCINTBIT); 				// set (un-mask) PCINTn pin for interrupt on change
	PCICR |= _BV(BTNS1_PCICRBIT); 						// enable wanted PCICR

	BTNS2_DDR &= ~_BV(BTNS2_PIN); 						// pin is input
	BTNS2_PORT |= _BV(BTNS2_PIN); 						// turn ON internal pullup
	BTNS2_PCMSKREG |= _BV(BTNS2_PCINTBIT); 				// set (un-mask) PCINTn pin for interrupt on change
	PCICR |= _BV(BTNS2_PCICRBIT); 						// enable wanted PCICR

	BTNS3_DDR &= ~_BV(BTNS3_PIN); 						// pin is input
	BTNS3_PORT |= _BV(BTNS3_PIN); 						// turn ON internal pullup
	BTNS3_PCMSKREG |= _BV(BTNS3_PCINTBIT); 				// set (un-mask) PCINTn pin for interrupt on change
	PCICR |= _BV(BTNS3_PCICRBIT); 						// enable wanted PCICR

	// Optocouplers S0..S3
	setOutput(S0_DDR, S0_PIN);
	setLow(S0_PORT, S0_PIN);

	setOutput(S1_DDR, S1_PIN);
	setLow(S1_PORT, S1_PIN);

	setOutput(S2_DDR, S2_PIN);
	setLow(S2_PORT, S2_PIN);

	setOutput(S3_DDR, S3_PIN);
	setLow(S3_PORT, S3_PIN);

	// LEDs
	setOutput(LEDA_DDR, LEDA_PIN);
	setLow(LEDA_PORT, LEDA_PIN);

	setOutput(LEDB_DDR, LEDB_PIN);
	setLow(LEDB_PORT, LEDB_PIN);

	setOutput(LEDC_DDR, LEDC_PIN);
	setLow(LEDC_PORT, LEDC_PIN);
}

//////////////////////////////////// LED_HELPERS

// LED helpers
void leda_on() {
	setHigh(LEDA_PORT, LEDA_PIN);
}
void leda_off() {
	setLow(LEDA_PORT, LEDA_PIN);
}
void leda_blink(uint8_t times) {
	while(times--) {
		setHigh(LEDA_PORT, LEDA_PIN);
		delay_ms_(200);
		setLow(LEDA_PORT, LEDA_PIN);
		delay_ms_(200);
	}
}
void ledb_on() {
	setHigh(LEDB_PORT, LEDB_PIN);
}
void ledb_off() {
	setLow(LEDB_PORT, LEDB_PIN);
}
void ledb_blink(uint8_t times) {
	while(times--) {
		setHigh(LEDB_PORT, LEDB_PIN);
		delay_ms_(200);
		setLow(LEDB_PORT, LEDB_PIN);
		delay_ms_(200);
	}
}
void ledc_on() {
	setHigh(LEDC_PORT, LEDC_PIN);
}
void ledc_off() {
	setLow(LEDC_PORT, LEDC_PIN);
}
void ledc_blink(uint8_t times) {
	while(times--) {
		setHigh(LEDC_PORT, LEDC_PIN);
		delay_ms_(200);
		setLow(LEDC_PORT, LEDC_PIN);
		delay_ms_(200);
	}
}

void led_isrblink(uint8_t isr_led_mask, uint16_t rate) {
	if(!rate) {
		isr_led_blinker &= ~isr_led_mask;
	}
	else {
		isr_led_blinker |= isr_led_mask;
		isr_led_blinker_rate = rate;
		isr_led_blinker_tmr = 0;
	}
}

//////////////////////////////////// END: LED_HELPERS

//////////////////////////////////// KEELOQ_PROG_LIB_CALLBACKS

// when programming has started
void keeloq_prog_init_hw(uint8_t prog0_verify1) {
	HCS_PROG_S2CLK_DDR |= _BV(HCS_PROG_S2CLK_PIN); // clock pin is always output
	HCS_PROG_S2CLK_PORT &= ~_BV(HCS_PROG_S2CLK_PIN); // =0

	HCS_PROG_S0S1_DDR |= _BV(HCS_PROG_S0S1_PIN); // S0 & S1 pins - output
	HCS_PROG_S0S1_PORT &= ~_BV(HCS_PROG_S0S1_PIN); // =0
	
	HCS_PROG_S3_DDR |= _BV(HCS_PROG_S3_PIN); // S3 pin - output
	HCS_PROG_S3_PORT &= ~_BV(HCS_PROG_S3_PIN); // =0

	// programming? data pin is output
	if(prog0_verify1 == 0) {
		HCS_PROG_DATA_DDR |= _BV(HCS_PROG_DATA_PIN);
	}
	// verification - data pin is input
	else {
		HCS_PROG_DATA_DDR &= ~_BV(HCS_PROG_DATA_PIN);
	}
	HCS_PROG_DATA_PORT &= ~_BV(HCS_PROG_DATA_PIN); // =0 (or disable pullup when input)
}

// setting clock pin for programmer
void keeloq_prog_set_clk_pin_hw(uint8_t pin_state) {
	if(pin_state) {
		HCS_PROG_S2CLK_PORT |= _BV(HCS_PROG_S2CLK_PIN);
	}
	else {
		HCS_PROG_S2CLK_PORT &= ~_BV(HCS_PROG_S2CLK_PIN);
	}
}

// setting data pin for programmer
void keeloq_prog_set_data_pin_hw(uint8_t pin_state) {
	if(pin_state) {
		HCS_PROG_DATA_PORT |= _BV(HCS_PROG_DATA_PIN);
	}
	else {
		HCS_PROG_DATA_PORT &= ~_BV(HCS_PROG_DATA_PIN);
	}
}

// getting data pin for programmer
uint8_t keeloq_prog_get_data_pin_hw() {
	return !!(HCS_PROG_DATA_PINREG & _BV(HCS_PROG_DATA_PIN));
}

// when programming has ended
void keeloq_prog_deinit_hw() {
	// set back all pins to high impedance state - inputs
	HCS_PROG_S2CLK_DDR &= ~_BV(HCS_PROG_S2CLK_PIN);
	HCS_PROG_S2CLK_PORT &= ~_BV(HCS_PROG_S2CLK_PIN); // pullup off

	HCS_PROG_DATA_DDR &= ~_BV(HCS_PROG_DATA_PIN);
	HCS_PROG_DATA_PORT &= ~_BV(HCS_PROG_DATA_PIN); // pullup off

	// and S0&S1,S3 also
	HCS_PROG_S0S1_DDR &= ~_BV(HCS_PROG_S0S1_PIN); // S0 & S1 pins - input
	HCS_PROG_S0S1_PORT &= ~_BV(HCS_PROG_S0S1_PIN); // pullup off
	HCS_PROG_S3_DDR &= ~_BV(HCS_PROG_S3_PIN); // S3 pin - input
	HCS_PROG_S3_PORT &= ~_BV(HCS_PROG_S3_PIN); // pullup off
}

//////////////////////////////////// END: KEELOQ_PROG_LIB_CALLBACKS

//////////////////////////////////// KEELOQ_LIB_CALLBACKS

// when receiving is started
void keeloq_rx_init_hw() {
	// init RF RX pin to interrupt on-change
	RX_DDR &= ~_BV(RX_PIN); 						// pin is input
	RX_PORT |= _BV(RX_PIN); 						// turn ON internal pullup
	RX_PCMSKREG |= _BV(RX_PCINTBIT); 				// set (un-mask) PCINTn pin for interrupt on change
	PCICR |= _BV(RX_PCICRBIT); 						// enable wanted PCICR
}

// when receiving is stopped
void keeloq_rx_deinit_hw() {
	PCICR &= ~_BV(RX_PCICRBIT); // disable interrupts for pin-change, but leave pin as input
}

// when transmission is started
void keeloq_init_tx_hw() {
	TX_DDR |= _BV(TX_PIN); // pin is output
	TX_PORT &= ~_BV(TX_PIN); // low output
}

// when transmission is stopped
void keeloq_deinit_tx_hw() {
	// lets switch pin to input, just in case
	TX_DDR &= ~_BV(TX_PIN); // pin is input
	TX_PORT &= ~_BV(TX_PIN); // no pullup
}

// for turning TX pin on/off
void keeloq_pin_tx_hw(uint8_t pin_state) {
	if(pin_state) {
		TX_PORT |= _BV(TX_PIN); // =1
	}
	else {
		TX_PORT &= ~_BV(TX_PIN); // =0
	}
}

//////////////////////////////////// END: KEELOQ_LIB_CALLBACKS

// interrupt based delay function
void delay_ms_(uint64_t ms) {
	delay_milliseconds = ms;

	// waiting until the end...
	while(delay_milliseconds > 0);
}

//##############################
// Interrupt: TIMER0 OVERFLOW //
//##############################
// set to overflow at 1.024 millisecond
ISR(TIMER0_OVF_vect, ISR_NOBLOCK)
{
	milliseconds++; // these are actually 1.024ms

	if(delay_milliseconds) delay_milliseconds--;	// for the isr based delay
	if(btn_hold_timer) btn_hold_timer--;			// to detect button holds, we shouldn't overflow!
	if(btn_expect_timer) btn_expect_timer--;		// to detect button idling
	if(action_expecter_timer) action_expecter_timer--; // for expecting misc actions

	// blinking LEDs from ISR. limitation: all leds can blink at the same rate at any time
	if(isr_led_blinker) {
		isr_led_blinker_tmr--;
		// time to toggle LED(s)?
		if (isr_led_blinker_tmr == 0) {
			isr_led_blinker_tmr = isr_led_blinker_rate; // reload for toggling
			if (isr_led_blinker & ISR_LED_A_MASK) {
				togglePin(LEDA_PORT, LEDA_PIN);
			}
			if (isr_led_blinker & ISR_LED_B_MASK) {
				togglePin(LEDB_PORT, LEDB_PIN);
			}
			if (isr_led_blinker & ISR_LED_C_MASK) {
				togglePin(LEDC_PORT, LEDC_PIN);
			}
		}
	}
}

// Interrupt: PWM process, output bit transitioned from 1 -> 0
// FOR TRANSMITTER
ISR(TIMER1_COMPA_vect, ISR_NOBLOCK)
{
	// process of clocking out data to output pin for the transmitter
	kl_tx_process(&kl_ctx);
}

// Interrupt: TIMER1 CTC EVENT
// FOR RECEIVER
ISR(TIMER1_CAPT_vect, ISR_NOBLOCK)
{
	// tell library that pulse measurement has timed out
	kl_rx_pulse_timeout(&kl_ctx);
}

// Interrupt: pin change interrupt
// FOR RECEIVER
ISR(PCINT0_vect, ISR_NOBLOCK)
{
	// this is the only pin-change interrupt on PCINT2_vector, so we don't need
	// to check if it is receiver or something else, because it IS the receiver

	// receiving a bit of transmission stream
	kl_rx_process(&kl_ctx, !!(RX_PINREG & _BV(RX_PIN)));
}

// Interrupt: pin change interrupt
// FOR BUTTONS
ISR(PCINT2_vect, ISR_NOBLOCK) { // this interrupt routine can be interrupted by any other, and MUST BE!
	// in here we are relying on delay_ms_() which is actually an interrupt driven delay function, so ISR must be enabled past this point. we do that with ISR_NOBLOCK parameter above

	PCICR &= ~_BV(PCIE2); 								// ..disable interrupts for the entire section

	// check to see if button S0 is pressed
	if( !(BTNS0_PINREG & _BV(BTNS0_PIN)) )
	{
		// see if it wasn't pressed at all
		if ( !(prev_btn & BTNS0_MASK) )
		{
			delay_ms_(25); // debounce
			if( BTNS0_PINREG & _BV(BTNS0_PIN) )			// if it is still not GND after 25ms, this was an error
			{
				PCICR |= _BV(PCIE2); 					// ..re-enable interrupts for the entire section
				return;
			}
			prev_btn |= BTNS0_MASK; 					// remember it was pressed

			btn_hold_timer = BTN_HOLD_TMR; 				// load for hold detection
			while(btn_hold_timer > 0)
			{
				// button released in the meantime
				if( BTNS0_PINREG & _BV(BTNS0_PIN) )
				{
					btn_press |= BTNS0_MASK; 			// this was just a press!

					PCICR |= _BV(PCIE2); 				// ..re-enable interrupts for the entire section
					return;
				}
			}
			// if we get to this line, the button is still pressed, so "it is held"
			btn_hold |= BTNS0_MASK;
		}
	}
	// button not pressed at this ISR event
	else
	{
		prev_btn &= ~BTNS0_MASK;
	}

	// check to see if button S1 is pressed
	if( !(BTNS1_PINREG & _BV(BTNS1_PIN)) )
	{
		// see if it wasn't pressed at all
		if ( !(prev_btn & BTNS1_MASK) )
		{
			delay_ms_(25); // debounce
			if( BTNS1_PINREG & _BV(BTNS1_PIN) )			// if it is still not GND after 25ms, this was an error
			{
				PCICR |= _BV(PCIE2); 					// ..re-enable interrupts for the entire section
				return;
			}
			prev_btn |= BTNS1_MASK; 					// remember it was pressed

			btn_hold_timer = BTN_HOLD_TMR; 				// load for hold detection
			while(btn_hold_timer > 0)
			{
				// button released in the meantime
				if( BTNS1_PINREG & _BV(BTNS1_PIN) )
				{
					btn_press |= BTNS1_MASK; 			// this was just a press!

					PCICR |= _BV(PCIE2); 				// ..re-enable interrupts for the entire section
					return;
				}
			}

			// if we get to this line, the button is still pressed, so "it is held"
			btn_hold |= BTNS1_MASK;
		}
	}
	// button not pressed at this ISR event
	else
	{
		prev_btn &= ~BTNS1_MASK;
	}

	// check to see if button S2 is pressed
	if( !(BTNS2_PINREG & _BV(BTNS2_PIN)) )
	{
		// see if it wasn't pressed at all
		if ( !(prev_btn & BTNS2_MASK) )
		{
			delay_ms_(25); // debounce
			if( BTNS2_PINREG & _BV(BTNS2_PIN) )			// if it is still not GND after 25ms, this was an error
			{
				PCICR |= _BV(PCIE2); 					// ..re-enable interrupts for the entire section
				return;
			}
			prev_btn |= BTNS2_MASK; 					// remember it was pressed

			btn_hold_timer = BTN_HOLD_TMR; 				// load for hold detection
			while(btn_hold_timer > 0)
			{
				// button released in the meantime
				if( BTNS2_PINREG & _BV(BTNS2_PIN) )
				{
					btn_press |= BTNS2_MASK; 			// this was just a press!

					PCICR |= _BV(PCIE2); 				// ..re-enable interrupts for the entire section
					return;
				}
			}
			// if we get to this line, the button is still pressed, so "it is held"
			btn_hold |= BTNS2_MASK;
		}
	}
	// button not pressed at this ISR event
	else
	{
		prev_btn &= ~BTNS2_MASK;
	}

	// check to see if button S3 is pressed
	if( !(BTNS3_PINREG & _BV(BTNS3_PIN)) )
	{
		// see if it wasn't pressed at all
		if ( !(prev_btn & BTNS3_MASK) )
		{
			delay_ms_(25); // debounce
			if( BTNS3_PINREG & _BV(BTNS3_PIN) )			// if it is still not GND after 25ms, this was an error
			{
				PCICR |= _BV(PCIE2); 					// ..re-enable interrupts for the entire section
				return;
			}
			prev_btn |= BTNS3_MASK; 					// remember it was pressed

			btn_hold_timer = BTN_HOLD_TMR; 				// load for hold detection
			while(btn_hold_timer > 0)
			{
				// button released in the meantime
				if( BTNS3_PINREG & _BV(BTNS3_PIN) )
				{
					btn_press |= BTNS3_MASK; 			// this was just a press!

					PCICR |= _BV(PCIE2); 				// ..re-enable interrupts for the entire section
					return;
				}
			}
			// if we get to this line, the button is still pressed, so "it is held"
			btn_hold |= BTNS3_MASK;
		}
	}
	// button not pressed at this ISR event
	else
	{
		prev_btn &= ~BTNS3_MASK;
	}

	PCICR |= _BV(PCIE2); 								// ..re-enable interrupts for the entire section
}

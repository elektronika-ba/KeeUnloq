/*
 * keeloq.c
 *
 * Created: 11. 2. 2021. 19:10:43
 *  Author: Trax, www.elektronika.ba
 * 
 * Hardware dependencies:
 * - pin change interrupt
 * - 16bit Timer1 for pulse length measurement for receiver
 * - PWM mode 14 for transmitter
 * - OCR1A output for transmitter
 * - util/delay.h of AVR or whatever else can create 10us delays
 * - avr/io.h because of Timer1 registers
 * 
 * Hardly portable to other platforms.
 * 
 */ 

#include "keeloq.h"

// built-in delay wrapper
void delay_10us_(uint16_t delay_us) {
	while(delay_us--) {
		_delay_us(10);
	}
}

void kl_init_ctx(volatile struct keeloq_ctx *ctx) {
	ctx->kl_tx_state = KL_TX_IDLE;
	ctx->kl_rx_state = KL_RX_STOP;
	
	ctx->kl_rx_process_busy = 0;
	ctx->kl_rx_pulse_timeout_busy = 0;
	ctx->kl_tx_process_busy = 0;
}

// keeloq transmit preamble
void kl_tx_preamble(volatile struct keeloq_ctx *ctx, uint16_t timing_element_us, uint8_t preamble_size) {
	// preamble is preamble_size * (50% PWM of 2*timing_element_us)
	
	// it is easy to do this one here manually
	while(preamble_size-- > 0) {
		delay_10us_(timing_element_us/10);
		ctx->fn_tx_pin_hw(1);
		delay_10us_(timing_element_us/10);
		ctx->fn_tx_pin_hw(0);
	}
}

// called every time PWM output has changed from 1 -> 0 in order to advance to the next bit until we detect that we clocked out all bits required
void kl_tx_process(volatile struct keeloq_ctx *ctx) {
	//if(ctx->kl_tx_state != KL_TX_BUSY) return; // prevent working while in invalid state

	if(ctx->kl_tx_process_busy) return;
	ctx->kl_tx_process_busy = 1;

	// the transmission happens LSb first so lets save it that way in our buffer
	uint8_t arr_index = ctx->kl_tx_buff_bit_index / 8;
	uint8_t arr_bit_index = ctx->kl_tx_buff_bit_index % 8;
	
	uint8_t buff_byteval = ctx->kl_tx_buff[arr_index];
	if(arr_bit_index > 0) {
		buff_byteval = (buff_byteval >> arr_bit_index);
	}

	// 1 = 1xTE
	if(buff_byteval & 0b00000001) {
		// WARNING: this is where hardware abstraction is not possible
		OCR1A = 1 * (ctx->kl_tx_timing_element * 2); // 1xTE but converted to 0.5us
	}
	// 0 = 2xTE
	else {
		// WARNING: this is where hardware abstraction is not possible
		OCR1A = 2 * (ctx->kl_tx_timing_element * 2); // 2xTE but converted to 0.5us
	}

	/*char xxx[32];
	sprintf(xxx, "%u ", OCR1A);
	uart_puts(xxx);*/

	// time to end the transmission process
	if(ctx->kl_tx_buff_bit_index >= ctx->kl_tx_bitlen) {
		// WARNING: this is where hardware abstraction is not possible
		TCCR1A = 0; // stop!
		TCCR1B = 0; // stop!
		TIMSK1 &= ~_BV(OCIE1A);
		ctx->kl_tx_state = KL_TX_IDLE;
		//uart_puts("\r\n");
	}

	ctx->kl_tx_buff_bit_index++;
	
	ctx->kl_tx_process_busy = 0;
}

// keeloq transmit data
void kl_tx_data(volatile struct keeloq_ctx *ctx, uint8_t *buff, uint8_t bitlen, uint16_t timing_element_us) {
	// data is bitlen * (33% (bit1) or 66% (bit0) PWM of 3*timing_element_us)

	ctx->fn_tx_pin_hw(0);

	ctx->kl_tx_state = KL_TX_BUSY;
	ctx->kl_tx_buff_bit_index = 0;
	ctx->kl_tx_bitlen = bitlen;
	ctx->kl_tx_buff = buff; // point to the buffer holding the data
	ctx->kl_tx_timing_element = timing_element_us;

	// WARNING: this is where hardware abstraction is not possible
	// PWM frequency is 3xTE
	ICR1 = 3 * (timing_element_us * 2) ; // PWM frequency is 3xTE. *2 because timer runs in 0.5us steps so we need to double the value
	// Using: Timer1 in Fast PWM (mode 14)
	TCNT1 = 0;
	TCCR1A = _BV(COM1A1) | _BV(WGM11);
	TCCR1B = _BV(WGM13) | _BV(WGM12); // timer not running! important
	TIMSK1 |= _BV(OCIE1A); // OCIE1A is for ISR(TIMER1_COMPA_vect) -> output bit has went from 1 to 0, wanted duty cycle reached
	// start the process of transmission which advances to each new bit in the ISR of PWM process
	OCR1A = 0;
	TCNT1 = ICR1 - 1; // force ISR to trigger after timer's next tick
	TCCR1B |= _BV(CS11); // Timer1 running in F_CPU/8. for 16MHz that is 0.5us (500ns) per each value
}

// keeloq transmit
void kl_tx(volatile struct keeloq_ctx *ctx, uint8_t *buff, uint8_t bitlen, uint16_t timing_element_us, uint8_t preamble_size, uint16_t header_length_us, uint16_t guard_time_us) {
	ctx->fn_tx_init_hw();
	ctx->fn_tx_pin_hw(0);
	
	// transmit preamble
	kl_tx_preamble(ctx, timing_element_us, preamble_size);
	
	// pause for the header section
	delay_10us_(header_length_us/10);
	
	// transmit data
	kl_tx_data(ctx, buff, bitlen, timing_element_us);

	// wait until the PWM has been sent out
	while(ctx->kl_tx_state != KL_TX_IDLE) {
		//delay_10us_(1);
	}
	
	ctx->fn_tx_deinit_hw();
	
	// pause for the guard time
	delay_10us_(guard_time_us/10);
}

void kl_rx_pulse_timeout(volatile struct keeloq_ctx *ctx) {
	if(ctx->kl_rx_pulse_timeout_busy) return;
	ctx->kl_rx_pulse_timeout_busy = 1; // avoid nesting in here
	
	// pulse too long during reception of header
	if(ctx->kl_rx_state == KL_RX_HEADERCHECK) {
		ctx->kl_rx_state = KL_RX_SYNCING;
	}
	// pulse too long during reception of data stream
	// OR we actually received enough and this is the guard-time now so we need to finalize
	// we could have the 66, 67 or 69 bits received
	else if(ctx->kl_rx_state == KL_RX_RXING) {
		if(
			ctx->_kl_rx_buff_bit_index == 69
			||
			ctx->_kl_rx_buff_bit_index == 67
			||
			ctx->_kl_rx_buff_bit_index == 66
		) {
			// buffer empty? fill it in
			if(ctx->kl_rx_buff_state == KL_BUFF_EMPTY) {
				ctx->kl_rx_buff_state = KL_BUFF_FULL; // we are still not sure if transmitter stopped transmitting
				ctx->kl_rx_buff_bit_index = ctx->_kl_rx_buff_bit_index;
				memcpy((uint8_t *)ctx->kl_rx_buff, (uint8_t *)ctx->_kl_rx_buff, KL_BUFF_LEN);
			}
			
			// something is arriving
			ctx->kl_rx_rf_act = KL_RF_ACT_BUSY;

			// state machine will restart
			ctx->kl_rx_state = KL_RX_SYNCING;

			// if we miss "this many headers", we will call it end of transmission
			ctx->kl_rx_guard_timer = KL_GUARD_TIMER_CNT; // how many of these timeouts do we allow before pronouncing actual end of transmission
		}
		else {
			ctx->kl_rx_state = KL_RX_SYNCING;
		}
	}
	// for expecting end of transmission
	else if(ctx->kl_rx_state == KL_RX_SYNCING) {
		if(ctx->kl_rx_guard_timer) {
			ctx->kl_rx_guard_timer--;
			if(!ctx->kl_rx_guard_timer) {
				ctx->kl_rx_rf_act = KL_RF_ACT_IDLE;
			}
		}
	}
	
	ctx->kl_rx_pulse_timeout_busy = 0;
}

// keeloq initializing timer and pin-change ISR
void kl_rx_start(volatile struct keeloq_ctx *ctx) {
	ctx->kl_rx_process_busy = 0;
	ctx->kl_rx_buff_state = KL_BUFF_EMPTY;
	ctx->kl_rx_state = KL_RX_SYNCING;
	ctx->kl_rx_rf_act = KL_RF_ACT_IDLE;

	// WARNING: this is where hardware abstraction is not possible
	// initialize Timer1 overflow ISR for pulse width measurement
	ICR1 = 0xFFFFU;
	TCCR1A = _BV(WGM11);
	TCCR1B = _BV(WGM13) | _BV(WGM12) | _BV(CS11); // Timer1 running in F_CPU/8. for 16MHz that is 0.5us (500ns) per each value. MODE OF OPERATION = CTC mode 14, counting to ICR1!
	TIMSK1 |= _BV(ICIE1); // ICIE1 is for ISR(TIMER1_CAPT_vect) ... TOIE1 is for ISR(TIMER1_OVF_vect)

	ctx->fn_rx_init_hw();
}

// keeloq stopping timer and pin-change ISR
void kl_rx_stop(volatile struct keeloq_ctx *ctx) {
	// WARNING: this is where hardware abstraction is not possible
	TIMSK1 &= ~_BV(ICIE1);
	TCCR1B = 0; // stop the Timer1

	ctx->fn_rx_deinit_hw();

	ctx->kl_rx_state = KL_RX_STOP;
	ctx->kl_rx_rf_act = KL_RF_ACT_IDLE;
}

// called after consuming the buffer
void kl_rx_flush(volatile struct keeloq_ctx *ctx) {
	//ctx->kl_rx_rf_act = KL_RF_ACT_IDLE;
	ctx->kl_rx_buff_state = KL_BUFF_EMPTY;
}

// keeloq receiving process, one bit at a time, PWM only, Manchester not implemented.
// this function must exit before next pin-change occurs, which is in some situations < 200us
// WARNING: this is where hardware abstraction is not possible
void kl_rx_process(volatile struct keeloq_ctx *ctx, uint8_t bit_val) {
	// measuring bit length is done by manipulating TIMER1
	// starting it, reseting it, reading it
	// it is setup so that it runs @ F_CPU/8 which for 16MHz is 0,5us (500ns)
	// so to get the microsecond value, shift to the left once (*2)
	
	if(ctx->kl_rx_state == KL_RX_STOP) return; // in case interrupt is still enabled but kl_rx_stop() was not called
	
	if(ctx->kl_rx_process_busy) return; // avoid nesting in here
	ctx->kl_rx_process_busy = 1;
	
	uint16_t w1us = TCNT1; // read the measurement which is currently in 0.5us values
	TCNT1 = 0; // reset timer to start the measurement again
	w1us = w1us / 2; // /2 to convert to 1us values from 0.5us
	
	switch(ctx->kl_rx_state) {
		// when last preamble bit finishes, from 1->0, we are starting measurement of the possible header length
		case KL_RX_SYNCING:
			if(!bit_val) {
				ICR1 = KL_HEADER_MAX_WIDTH_US * 2; // convert to 0.5us steps

				ctx->_kl_rx_buff_bit_index = 0;
				ctx->kl_rx_state = KL_RX_HEADERCHECK;
			}
		break;

		// header measurement, this can only be a transition from 0->1, we set it up so in the previous KL_RX_SYNCING stage
		case KL_RX_HEADERCHECK:
			// possible HEADER ended, let's verify it and figure out the actual TE length from it, since TE = TH/10	
			if(w1us >= KL_HEADER_MIN_WIDTH_US && w1us <= KL_HEADER_MAX_WIDTH_US) {
				// this was a header that just passed, and we are now at the positive impulse of the first data-bit

				// flush rx buffer
				for(uint8_t i = 0; i < KL_BUFF_LEN; i++) {
					ctx->_kl_rx_buff[i] = 0;
				}
				
				ctx->kl_rx_header_length = w1us;
				
				// stupid crap, I am receiving from 7 to 14 TEs in TH field. I can't rely on TH/10 to get the TE from there.
				ctx->kl_rx_timing_element_min = w1us / 14; // from my measurements
				ctx->kl_rx_timing_element_max = ctx->kl_rx_timing_element_min * 2;

				// from now on transitions happen in maximum of 4 x TE, else we have an error
				ICR1 = (4 * ctx->kl_rx_timing_element_max) * 2; // convert to 0.5us steps

				ctx->kl_rx_state = KL_RX_RXING;
			}
			else {
				ctx->kl_rx_state = KL_RX_SYNCING;
			}
		break;

		// receiving the data
		case KL_RX_RXING:
			// transition from 1->0
			if(!bit_val) {
				// we received more bits that we are capable of storing in memory? reject!
				if(ctx->_kl_rx_buff_bit_index > (KL_BUFF_LEN * 8) - 1) {
					ctx->kl_rx_state = KL_RX_SYNCING;
					ctx->kl_rx_process_busy = 0;
					return;
				}

				// end of a bit, decode it to 0/1
				uint16_t t1TEmin = 1 * ctx->kl_rx_timing_element_min;
				uint16_t t1TEmax = 1 * ctx->kl_rx_timing_element_max;
				uint16_t t2TEmin = 2 * ctx->kl_rx_timing_element_min;
				uint16_t t2TEmax = 2 * ctx->kl_rx_timing_element_max;

				// 1 (1 x TE(high))
				if(w1us >= t1TEmin && w1us <= t1TEmax) {
					ctx->kl_rx_timing_element = w1us; // remember, if we need it elsewhere

					// add decoded bit into our kl_buff array
					uint8_t arr_index = ctx->_kl_rx_buff_bit_index / 8;
					uint8_t arr_bit_index = ctx->_kl_rx_buff_bit_index % 8;
					ctx->_kl_rx_buff[arr_index] |= (0x01 << arr_bit_index);
				}
				// 0 (2 x TE(high))
				else if (w1us >= t2TEmin && w1us <= t2TEmax) {
					// we don't process zeros
					// but we catch them here for validation purposes only
				}
				// invalid bit length - reject everything
				else {
					/*char tmp[64];
					sprintf(tmp, "E(%u), RX=%u, TE=%u, MIN=%u, MAX=%u\r\n", ctx->_kl_rx_buff_bit_index, w1us, ctx->kl_rx_timing_element, ctx->kl_rx_timing_element_min, ctx->kl_rx_timing_element_max);
					uart_puts(tmp);*/
					
					ctx->kl_rx_state = KL_RX_SYNCING;
					ctx->kl_rx_process_busy = 0;
					return;
				}

				// we are handling only HCS* KeeLoq series so we can receive 66, 67 or 69 bits here, we don't know
				// in advance so we let the timeout of Timer1 decide on this after the Guard Time has passed.
				// actually, after the ~ > 4xTE has passed without receiving a next positive pulse should do the trick
				// ICR1 is already set for that interval so we are good
			
				ctx->_kl_rx_buff_bit_index++;
			}
			// transition from 0->1
			else {
				// start of a new bit, we do nothing here
			}
		break;

		case KL_RX_STOP:
			// we are stopped, but ISR is still running for some reason
		break;
		
		// should not happen
		default:
			ctx->kl_rx_state = KL_RX_SYNCING;
	}
	
	ctx->kl_rx_process_busy = 0;
}

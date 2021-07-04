/*
 * keeloq_prog.c
 *
 * Created: 21. 3. 2021. 19:56:41
 *  Author: Trax
 */ 

#include "keeloq_prog.h"

void kl_prog_init_ctx(volatile struct keeloq_prog_ctx *ctx) {
	// nothing here yet
}

// bit banger
uint8_t kl_prog(volatile struct keeloq_prog_ctx *ctx, unsigned char *stream, unsigned char bit_len, unsigned char verify) {
	ctx->fn_prog_init_hw(0); // init for programming (0)

	// prepare to go into the programing mode
	ctx->fn_set_clk_pin_hw(0);
	ctx->fn_set_data_pin_hw(0);
	_delay_ms(100);

	// enter the programming mode
	// Programming will be initiated by forcing the PWM line high, after the S2 line has been held high for the appropriate length of time
	ctx->fn_set_clk_pin_hw(1);
	_delay_ms(4); // TPS
	ctx->fn_set_data_pin_hw(1);
	_delay_ms(4); // TPH1
	ctx->fn_set_data_pin_hw(0);
	_delay_us(70); // TPH2
	ctx->fn_set_clk_pin_hw(0);
	_delay_ms(5); // TPBW

	// we should now be in the programming mode, and we can start bit-banging from the *stream buffer LSb LSB
	for(uint8_t bit_no = 0; bit_no < bit_len; bit_no++) {
		uint8_t arr_index = (bit_no / 8);
		uint8_t arr_bit_index = bit_no % 8;
		uint8_t stream_byteval = stream[arr_index];
		if(arr_bit_index > 0) {
			stream_byteval = (stream_byteval >> arr_bit_index);
		}

		// set the data bit to 0/1
		if(stream_byteval & 0b00000001) {
			ctx->fn_set_data_pin_hw(1);
		}
		else {
			ctx->fn_set_data_pin_hw(0);
		}
		_delay_us(50);
		
		// clock it out
		ctx->fn_set_clk_pin_hw(1);
		_delay_us(100); // TCLKH
		ctx->fn_set_clk_pin_hw(0);
		_delay_us(100); // TCLKL
		
		// switch to verification mode after last clocked-out bit, before the TWC delay! (to prevent short-circuiting if connected directly to PWM pin without a resistor)
		if(bit_no == bit_len - 1) {
			ctx->fn_prog_init_hw(1); // re-init for verification (1)
		}
		
		// on every 16 clocked-out bits we need to make a TWC delay
		if(!((bit_no + 1) % 16)) {
			_delay_ms(60); // TWC
		}
	}

	// verification?
	if(verify) {
		// read back all programmed code
		// compare to what we expect in *stream
		// good: return 1, bad: return 0
		
		// make additional TWC if bit_len is not divisible by 16 bits
		if(bit_len % 16) {
			_delay_ms(60);
		}
		
		for(uint8_t bit_no = 0; bit_no < bit_len; bit_no++) {
			uint8_t arr_index = (bit_no / 8);
			uint8_t arr_bit_index = bit_no % 8;
			uint8_t stream_byteval = stream[arr_index];
			if(arr_bit_index > 0) {
				stream_byteval = (stream_byteval >> arr_bit_index);
			}

			// compare bit by bit, it is easier
			_delay_us(60);

			// clock high			
			ctx->fn_set_clk_pin_hw(1);
			_delay_us(60);
			
			// read bit from the chip
			uint8_t bit_val = ctx->fn_get_data_pin_hw();
			
			// not the same as expected? abort
			if(bit_val != (stream_byteval & 0b00000001)) {
				verify = 0;
				break;
			}
			
			// clock low
			_delay_us(100);
			ctx->fn_set_clk_pin_hw(0);
		}
	}
	else {
		verify = 1;
	}

	ctx->fn_prog_deinit_hw();
	
	return verify;
}

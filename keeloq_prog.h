/*
 * keeloq_prog.h
 *
 * Created: 21. 3. 2021. 19:56:31
 *  Author: Trax
 */ 

#ifndef KEELOQ_PROG_H_
#define KEELOQ_PROG_H_

#include <stdio.h>
#include <util/delay.h>

// KeeLoq programmer context
struct keeloq_prog_ctx {
	// hardware related callbacks
	void (*fn_prog_init_hw)(uint8_t prog0_verify1);
	void (*fn_prog_deinit_hw)();

	void (*fn_set_clk_pin_hw)(uint8_t pin_state);
	void (*fn_set_data_pin_hw)(uint8_t pin_state);
	uint8_t (*fn_get_data_pin_hw)(void);
};

// to init myself
void kl_prog_init_ctx(volatile struct keeloq_prog_ctx *);

uint8_t kl_prog(volatile struct keeloq_prog_ctx *, unsigned char *, unsigned char, unsigned char);

#endif /* KEELOQ_PROG_H_ */
/*
 * keeloq.h
 *
 * Created: 11. 2. 2021. 19:11:38
 *  Author: Trax, www.elektronika.ba
 */ 

#ifndef KEELOQ_H_
#define KEELOQ_H_

#include <stdio.h>
#include <avr/io.h>
#include <util/delay.h>
#include <string.h>

#define KL_TE_WIDTH_MIN_US					(190) // the shortest pulse we accept
#define KL_TE_WIDTH_MAX_US					(620) // the longest pulse we accept. note: some cheap RF receivers stretch the pulse to as much as 50%

#define KL_HEADER_MIN_WIDTH_US				(10 * KL_TE_WIDTH_MIN_US) // minimum TH allowed. 10 x MINIMUM(TE)
#define KL_HEADER_MAX_WIDTH_US				(10 * KL_TE_WIDTH_MAX_US) // maximum TH allowed. 10 x MAXIMUM(TE)

#define KL_GUARD_TIMER_CNT					(20) // how many KL_HEADER_MAX_WIDTH_US do we allow to pass before we pronounce end of RF activity

#define KL_BUFF_LEN							(9) // shoud remain at 9 (enough for handling 72 bits of data which is OK for entire old HCS* series of KeeLoq)

enum KL_RX_STATE
{
	KL_RX_STOP = 0,
	KL_RX_SYNCING = 1,
	KL_RX_HEADERCHECK = 2,
	KL_RX_RXING = 3,
};

enum KL_RF_ACT
{
	KL_RF_ACT_IDLE = 0, // nothing is being received
	KL_RF_ACT_BUSY = 1, // something is being received
};

enum KL_BUFF_STATE
{
	KL_BUFF_EMPTY = 0,
	KL_BUFF_FULL = 1,
};

enum KL_TX_STATE
{
	KL_TX_IDLE = 0,
	KL_TX_BUSY = 1,
};

// KeeLoq TRX context
struct keeloq_ctx {
	// RX
	enum KL_RX_STATE kl_rx_state;
	uint16_t kl_rx_header_length;
	uint16_t kl_rx_timing_element;
	uint16_t kl_rx_timing_element_min;
	uint16_t kl_rx_timing_element_max;
	uint8_t kl_rx_guard_timer;
	uint8_t kl_rx_buff_bit_index;
	uint8_t _kl_rx_buff_bit_index; // internal usage
	uint8_t _kl_rx_buff[KL_BUFF_LEN]; // internal buffer for actual receiving

	enum KL_RF_ACT kl_rx_rf_act; // rf activity

	enum KL_BUFF_STATE kl_rx_buff_state;
	uint8_t kl_rx_buff[KL_BUFF_LEN]; // external buffer for consuming from outside

	// TX
	enum KL_TX_STATE kl_tx_state;
	uint8_t kl_tx_buff_bit_index;
	uint8_t kl_tx_bitlen;
	uint8_t *kl_tx_buff;
	uint16_t kl_tx_timing_element;

	// functions called by ISRs should not nest
	uint8_t kl_rx_process_busy;
	uint8_t kl_rx_pulse_timeout_busy;
	uint8_t kl_tx_process_busy;

	// hardware related callbacks
	void (*fn_rx_init_hw)();
	void (*fn_rx_deinit_hw)();
	void (*fn_tx_init_hw)();
	void (*fn_tx_deinit_hw)();
	void (*fn_tx_pin_hw)(uint8_t pin_state);
};

// to init myself
void kl_init_ctx(volatile struct keeloq_ctx *);

// receiver
void kl_rx_start(volatile struct keeloq_ctx *);
void kl_rx_process(volatile struct keeloq_ctx *, uint8_t); // called on each pin-change ISR of the RF receiver
void kl_rx_stop(volatile struct keeloq_ctx *);
void kl_rx_flush(volatile struct keeloq_ctx *);
void kl_rx_pulse_timeout(volatile struct keeloq_ctx *);

// transmitter
void kl_tx(volatile struct keeloq_ctx *, uint8_t *, uint8_t, uint16_t, uint8_t, uint16_t, uint16_t);
void kl_tx_preamble(volatile struct keeloq_ctx *, uint16_t, uint8_t);
void kl_tx_data(volatile struct keeloq_ctx *, uint8_t *, uint8_t, uint16_t);
void kl_tx_process(volatile struct keeloq_ctx *); // called by the PWM output ISR

#endif /* KEELOQ_H_ */
/*
 * keeloq_decode.h
 *
 * Created: 27. 5. 2021. 11:23:07
 *  Author: Trax
 */ 


#ifndef KEELOQ_DECODE_H_
#define KEELOQ_DECODE_H_

#include <string.h>

#include "keeloq_crypt.h"

// Encoder ICs
// these constants are also used by the eeprom database, so don't change these unless you will clear your eeprom memory database as well
#define ENCODER_INVALID		0
#define ENCODER_HCS101		1
#define ENCODER_HCS200		2
#define ENCODER_HCS201		3
#define ENCODER_HCS300		4
#define ENCODER_HCS301		5
#define ENCODER_HCS320		6
#define ENCODER_HCS360		7
#define ENCODER_HCS361		8
#define ENCODER_HCS362		9
#define ENCODER_UNKNOWN		255

// CONFIG WORD BITS
// HCS200
#define HCS200_CONFIG_DISC_0		0
#define HCS200_CONFIG_DISC_1		1
#define HCS200_CONFIG_DISC_2		2
#define HCS200_CONFIG_DISC_3		3
#define HCS200_CONFIG_DISC_4		4
#define HCS200_CONFIG_DISC_5		5
#define HCS200_CONFIG_DISC_6		6
#define HCS200_CONFIG_DISC_7		7
#define HCS200_CONFIG_DISC_8		8
#define HCS200_CONFIG_DISC_9		9
#define HCS200_CONFIG_DISC_10		10
#define HCS200_CONFIG_DISC_11		11
#define HCS200_CONFIG_VLOW			12 // 0=6V, 1=12V
#define HCS200_CONFIG_BSL0			13 // 0=400us, 1=200us

// HCS201
#define HCS201_CONFIG_OSC_0		0
#define HCS201_CONFIG_OSC_1		1
#define HCS201_CONFIG_OSC_2		2
#define HCS201_CONFIG_OSC_3		3
#define HCS201_CONFIG_VLOWS		4
#define HCS201_CONFIG_BRS		5
#define HCS201_CONFIG_MTX4		6
#define HCS201_CONFIG_TXEN		7
#define HCS201_CONFIG_S3SET		8
#define HCS201_CONFIG_XSER		9

// HCS300 301 320
#define HCS300_301_320_CONFIG_DISC_0		0
#define HCS300_301_320_CONFIG_DISC_1		1
#define HCS300_301_320_CONFIG_DISC_2		2
#define HCS300_301_320_CONFIG_DISC_3		3
#define HCS300_301_320_CONFIG_DISC_4		4
#define HCS300_301_320_CONFIG_DISC_5		5
#define HCS300_301_320_CONFIG_DISC_6		6
#define HCS300_301_320_CONFIG_DISC_7		7
#define HCS300_301_320_CONFIG_DISC_8		8
#define HCS300_301_320_CONFIG_DISC_9		9
#define HCS300_301_320_CONFIG_OVR_0			10
#define HCS300_301_320_CONFIG_OVR_1			11
#define HCS300_301_320_CONFIG_VLOW			12 // 0=6V, 1=12V
#define HCS300_301_320_CONFIG_BSL0			13
#define HCS300_301_320_CONFIG_BSL1			14

// HCS360 and 361
#define HCS360_361_CONFIG_LNGRD_BACW		0
#define HCS360_361_CONFIG_BSEL0_BSEL		1
#define HCS360_361_CONFIG_BSEL1_TXWAK		2
#define HCS360_361_CONFIG_NU_SPM			3
#define HCS360_361_CONFIG_SEED_SEED			4
#define HCS360_361_CONFIG_DELM				5
#define HCS360_361_CONFIG_TIMO				6
#define HCS360_361_CONFIG_IND				7
#define HCS360_361_CONFIG_USRA0				8
#define HCS360_361_CONFIG_USRA1				9
#define HCS360_361_CONFIG_USRB0				10
#define HCS360_361_CONFIG_USRB1				11
#define HCS360_361_CONFIG_XSER				12
#define HCS360_361_CONFIG_TMPSD				13
#define HCS360_361_CONFIG_MOD				14
#define HCS360_361_CONFIG_OVR				15

// used for DECODE & ENCODE functions
struct KEELOQ_DECODE_PLAIN {
	// fixed portion
	uint8_t que;
	uint8_t crc;
	uint8_t repeat;
	uint8_t vlow;
	uint8_t buttons; // 0000 S2 S1 S0 S3
	uint32_t serial; // 28 lower bits used
	uint16_t serial3; // for hcs101

	// from encrypted portion (but unencrypted)
	uint8_t buttons_enc; // this is used only for decoding purposes. encoding uses "buttons"
	uint16_t disc; // 12 lower bits used out of which 2 upper bits might be OVR bits for some encoders
	uint16_t counter;
};

// used for programming HCS encoders
struct KEELOQ_DECODE_PROG_PROFILE {
	uint8_t encoder; // for which encoder is this profile. this is used when building the 192 bits of programming stream (to know how to build it for each encoder)

	uint64_t key;
	uint16_t sync;
	uint32_t ser;
	uint32_t seed;
	uint16_t seed2; // only for HCS360 and 361
	uint16_t config;
	uint16_t disc_hcs201; // only for HCS201
};

// public
uint8_t keeloq_decode(uint8_t *, uint8_t, uint64_t , struct KEELOQ_DECODE_PLAIN *);
void keeloq_encode(uint8_t, struct KEELOQ_DECODE_PLAIN *, uint64_t, uint8_t *);
void keeloq_decode_build_prog_stream(uint8_t *, struct KEELOQ_DECODE_PROG_PROFILE *);

// private
uint8_t keeloq_decode_calc_crc(uint8_t *);

#endif /* KEELOQ_DECODE_H_ */
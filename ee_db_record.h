/*
 * ee_db_record.h
 *
 * Created: 3. 5. 2021. 18:04:04
 *  Author: Trax
 */


#ifndef EE_DB_RECORD_H_
#define EE_DB_RECORD_H_

// NOTE: IF YOU HAVE DATA IN EEPROM, DO NOT CHANGE THIS AS IT WILL BREAK YOUR
// EEPROM DATA ORGANIZATION, AND YOU WILL HAVE TO PERFORM MEMORY FORMAT (CLEAR)
// OR CHANGE THE EEDB_FORMATTED_MAGIC VALUE AND RECOMPILE.

// this is saved in EEPROM as it stands here
// warning: do not re-arrange elements of this struct because it must match that in the EEPROM
struct eedb_hcs_record {
	uint8_t encoder;
	uint32_t serialno;
	uint64_t crypt_key;
	uint16_t counter;
	uint16_t counter_resync;
	uint16_t discriminator;

	uint16_t serial3no; // used for HCS101 only
	
	// used for re-transmission and sniffing
	uint8_t buttons;
	uint16_t timing_element;
	uint16_t header_length;
};

// this is saved in EEPROM as it stands here
// warning: do not re-arrange elements of this struct because it must match that in the EEPROM
struct eedb_log_record {
	// fixed and encrypted sections
	uint8_t kl_rx_buff[KL_BUFF_LEN];
};

#endif /* EE_DB_RECORD_H_ */

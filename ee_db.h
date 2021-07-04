/*
 * ee_db.h
 *
 * Created: 2. 5. 2021. 20:51:27
 *  Author: Trax
 */ 


#ifndef EE_DB_H_
#define EE_DB_H_

#include <stdio.h>
#include <util/delay.h>

#define EEDB_FORMATTED_MAGIC		0xBEEFDEAD	// marker that says if memory has been formatted or not. changing this will re-format the eeprom memory upon booting
#define EEDB_EEPROM_ADDR_SIZE		2			// 2 bytes for eeprom memory addressing
#define EEDB_INVALID_ADDR			0xFFFF
//#define EEDB_CACHE_SIZE			32			// how many addresses of records to cache
#define	EEDB_PKFK_ANY				0xFFFFFFFF	// * wildchar for PK and FK values during FIND/retrieval functions

// this is saved in EEPROM as it stands here
// warning: do not re-arrange elements of this struct because it must match that in the EEPROM
struct eedb_info {
	uint32_t formatted_magic;
};

// this is saved in EEPROM as it stands here
// warning: do not re-arrange elements of this struct because it must match that in the EEPROM
struct eedb_record_header {
	uint32_t pk; // primary key
	uint32_t fk; // foreign key
	uint8_t deleted; // if record is deleted
};

struct eedb_cache_element {
	uint32_t pk;
	uint32_t fk;
	uint16_t eeaddr; // byte-size of this element must match the EEDB_EEPROM_ADDR_SIZE
	uint8_t valid; // cache location valid or not
};

// EEPROM DB library context
struct eedb_ctx {
	uint8_t i2c_addr; // chip's i2c address with LSb ignored, as we will set/clear it (during read or write access)
	
	uint16_t start_eeaddr; // address where data begins in the EEPROM ICs memory. !ABSOLUTE EEPROM ADDRESS!
	uint16_t sizeof_record_entry;
	uint16_t record_capacity; // how many records do we store?

	uint16_t _allocated_bytes_eeaddr; // calculated at runtime
	uint16_t _next_free_eeaddr; // next free address in EEPROM space, after what we will take
	
	// caching EEPROM addresses of last N used records in RAM
	//struct eedb_cache_element _record_cache[EEDB_CACHE_SIZE];
	//uint8_t _cache_element_index; // for keeping track about which cache location has last been stored
	uint16_t _next_free_header_entry_eeaddr; // found at runtime
	
	//uint16_t _record_capacity; // calculated at runtime
	uint16_t _block_size; // calculated at runtime

	// populated at runtime, read from eeprom or initialized during formatting of memory
	struct eedb_info _eedb_info;

	// hardware related callbacks
	void (*fn_i2c_start)(uint8_t addr);
	void (*fn_i2c_tx)(uint8_t data);
	uint8_t (*fn_i2c_rx_ack)();
	uint8_t (*fn_i2c_rx_nack)();
	void (*fn_i2c_stop)();
};

void eedb_init_ctx(volatile struct eedb_ctx *);

//void eedb_invalidate_cache(volatile struct eedb_ctx *);
void eedb_update_info(volatile struct eedb_ctx *, struct eedb_info *);
void eedb_format_memory(volatile struct eedb_ctx *);
void eedb_read_record_by_eeaddr(volatile struct eedb_ctx *, uint16_t, struct eedb_record_header *, void *);
void eedb_write_record_by_eeaddr(volatile struct eedb_ctx *, uint16_t, struct eedb_record_header *, void *);
uint16_t eedb_find_free_record_eeaddr(volatile struct eedb_ctx *);

uint16_t eedb_find_record_eeaddr(volatile struct eedb_ctx *, uint32_t, uint32_t, uint16_t);
// INSERT
uint16_t eedb_insert_record(volatile struct eedb_ctx *, uint32_t, uint32_t, void *);
// UPDATE
uint8_t eedb_update_record(volatile struct eedb_ctx *, uint32_t, uint32_t, uint16_t, struct eedb_record_header *, void *);
// DELETE
uint8_t eedb_delete_record(volatile struct eedb_ctx *, uint32_t, uint32_t, uint16_t);
// UPSERT
uint8_t eedb_upsert_record(volatile struct eedb_ctx *, uint32_t, uint32_t, uint16_t, void *);

uint16_t eedb_for_each_record(volatile struct eedb_ctx *, uint32_t, uint32_t, void (*callback)(volatile struct eedb_ctx *, struct eedb_record_header *, void *), struct eedb_record_header *, void *);

//void eedb_print_headers(volatile struct eedb_ctx *);

/*
uint16_t eedb_find_free_eeaddr(volatile struct eedb_ctx *);
uint32_t eedb_upsert_record(volatile struct eedb_ctx *, struct eedb_header_entry *, void *, uint16_t);
void eedb_read_record(volatile struct eedb_ctx *, uint32_t, uint16_t, void *);
*/

void eedb_read_i2c(volatile struct eedb_ctx *, uint16_t, uint16_t, void *);
void eedb_write_i2c(volatile struct eedb_ctx *, uint16_t, uint8_t);
void eedb_write_n_i2c(volatile struct eedb_ctx *, uint16_t, uint16_t, void *);

#endif /* EE_DB_H_ */

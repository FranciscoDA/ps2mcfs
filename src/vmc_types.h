#ifndef _VMC_TYPES_H_
#define _VMC_TYPES_H_

#include <stdint.h>


typedef uint32_t cluster_t; // cluster index

typedef union {
	struct {
		cluster_t next_cluster: 31; // next cluster index in the linked list
		cluster_t occupied: 1;      // when flag is 1, the cluster at this index is allocated with data
	} entry;
	cluster_t raw; // the raw 32-bit value
} fat_entry_t; // type of an entry in the FAT table for a cluster

typedef uint32_t physical_offset_t; // absolute physical offset in bytes from the start of the memory card
typedef uint32_t logical_offset_t; // logical offset in bytes relative from the start of a cluster, file or directory entry

// A sentinel value to denote an invalid cluster index
static const cluster_t CLUSTER_INVALID = 0xFFFFFFFF;

// The sentinel value used to denote the end of a linked list in the FAT table
static const fat_entry_t FAT_ENTRY_TERMINATOR = { .raw = CLUSTER_INVALID };

// Value used to mark fat entries as unassigned
static const fat_entry_t FAT_ENTRY_FREE = { .entry = { .next_cluster = 0, .occupied = 0} };

typedef struct {
	char _unused;
	uint8_t second;
	uint8_t minute;
	uint8_t hour;
	uint8_t day;
	uint8_t month;
	uint16_t year;
} date_time_t;

typedef struct {
	uint16_t mode;
	uint16_t _unused0;
	uint32_t length;    // Length in bytes if a file, or entries if a directory. 
	date_time_t creation;
	cluster_t cluster;  // First cluster of the file, or 0xFFFFFFFF for an empty file. In "." entries this the first cluster of this directory's parent directory instead
	uint32_t dir_entry; // Only in "." entries. Entry of this directory in its parent's directory. 
	date_time_t modification;
	uint32_t attributes;
	char _unused1[28];
	char name[32];
	char _unused2[416];
} dir_entry_t;

typedef struct {
	dir_entry_t dirent;
	dir_entry_t parent; // parent dirent
	size_t index;
} browse_result_t;

enum card_flags {
    CF_USE_ECC = 0x01,      // Card supports ECC.
    CF_BAD_BLOCK = 0x08,    // Card may have bad blocks.
    CF_ERASE_ZEROES = 0x10, // Erased blocks have all bits set to zero.
};

typedef struct {
	char      magic[40]; // "Sony PS2 Memory Card Format "
	uint16_t  page_size;
	uint16_t  pages_per_cluster;
	uint16_t  pages_per_block;
	uint16_t  _unused0;
	uint32_t  clusters_per_card;
	cluster_t first_allocatable; // Cluster offset of the first allocatable cluster. Cluster values in the FAT and directory entries are relative to this.
	cluster_t last_allocatable;  // The cluster after the highest allocatable cluster. Relative to alloc_offset.
	cluster_t root_cluster;      // First cluster of the root directory. Relative to alloc_offset.
	uint32_t  backup_block1;     // Erase block used as a backup area during programming.
	uint32_t  backup_block2;     // This block should be erased to all ones.
	char _unused1[8];
	uint32_t indirect_fat_clusters[32];  // List of indirect FAT clusters indexes (relative to the start of the card)
	cluster_t bad_block_list[32];        // List of erase blocks that have errors and shouldn't be used.
	uint8_t   type;                      // Memory card type (Must be 2, indicating that this is a PS2 memory card.)
    uint8_t   card_flags; // Physical characteristics of the memory card. Allowed flags: CF_USE_ECC, CF_BAD_BLOCK, CF_ERASE_ZEROES
} superblock_t;

typedef struct {
	superblock_t* superblock;
	void* raw_data;
	size_t page_spare_area_size;
	uint8_t ecc_bytes;
} vmc_meta_t;

#ifdef DEBUG
#define DEBUG_printf(...) printf(__VA_ARGS__)
#else
#define DEBUG_printf(...)
#endif

#endif
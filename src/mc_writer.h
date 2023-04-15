#ifndef __MC_WRITER_H__
#define __MC_WRITER_H__

#include <stdio.h>
#include "vmc_types.h"


static const unsigned PAGE_SPARE_PART_SIZE = 16;

/**Writes an empty memory card file with the geometry described by the given superblock*/
int mc_writer_write_empty(const superblock_t* superblock, FILE* output_file);

#endif
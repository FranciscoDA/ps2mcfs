#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <stdbool.h>
#include <string.h>
#include "vmc_types.h"
#include "ecc.h"

static const struct option CLI_OPTIONS[] = {
    {.name = "size",   .has_arg = required_argument, .flag = NULL, .val = 0},
    {.name = "ecc",    .has_arg = no_argument,       .flag = NULL, .val = 0},
    {.name = "output", .has_arg = required_argument, .flag = NULL, .val = 0},
    {.name = "help",   .has_arg = no_argument,       .flag = NULL, .val = 0},
    {.name = NULL,     .has_arg = 0,                 .flag = NULL, .val = 0}
};

void usage(FILE* stream, const char* program_name, int exit_code) {
    fprintf(stream, "Usage: %s -o OUTPUT_FILE [-s SIZE] [-e] [-h]\n", program_name);
    exit(exit_code);
}

int copy_optarg(char** dest) {
    if (!optarg)
        return 0;
    size_t optarg_len = strlen(optarg);
    *dest = malloc(optarg_len);
    strcpy(*dest, optarg);
    return optarg_len;
}

int main(int argc, char** argv) {
    // initialize default superblock
    superblock_t superblock;
    memcpy(&superblock, &DEFAULT_SUPERBLOCK, sizeof(superblock_t));

    // parse options
    char* option_output_filename = NULL;
    int opt;
    int long_option_index = 0;
    while ((opt = getopt_long(argc, argv, "seo:h", CLI_OPTIONS, &long_option_index)) != -1) {
        // parse -s / --size option
        if ((opt == 0 && long_option_index == 0) || opt == 's') {
            if (strcmp(optarg, "8") == 0) {
                superblock.clusters_per_card = 8192;
            }
            else {
                fprintf(stderr, "Invalid SIZE value: %s. Allowed values: 8.\n", optarg);
                usage(stderr, argv[0], EXIT_FAILURE);
            }
        }
        // parse -e / --ecc option
        else if ((opt == 0 && long_option_index == 1) || opt == 'e') {
            superblock.card_flags |= CF_USE_ECC;
        }
        // parse -o / --output option
        else if ((opt == 0 && long_option_index == 2) || opt == 'o') {
            if (!copy_optarg(&option_output_filename)) {
                fprintf(stderr, "Invalid value: %s.\n", argv[optind]);
                usage(stderr, argv[0], EXIT_FAILURE);
            }
        }
        // parse -h / --help option
        else if ((opt == 0 && long_option_index == 3) || opt == 'h') {
            usage(stdout, argv[0], 0);
        }
        // handle invalid option
        else {
            fprintf(stderr, "Unrecognized option: %s.\n", argv[optind]);
            usage(stderr, argv[0], EXIT_FAILURE);
        }
    }

    if (option_output_filename == NULL) {
        fprintf(stderr, "Missing required argument: -o/--option\n");
        usage(stderr, argv[0], EXIT_FAILURE);
    }

    size_t physical_page_size = superblock.page_size;
    if (superblock.card_flags & CF_USE_ECC) {
        physical_page_size += 16; // byte spare area
    }
    uint8_t* page_buffer = malloc(physical_page_size);

    FILE* output_file = fopen(option_output_filename, "w");
    if (!output_file) {
        fprintf(stderr, "Could not open file for writing: %s\n", option_output_filename);
        exit(EXIT_FAILURE);
    }

    for (int cluster_i = 0; cluster_i < superblock.clusters_per_card; cluster_i++) {
        for (int page_i = 0; page_i < superblock.pages_per_cluster; page_i++) {
            bool is_indirect_fat_table_cluster = false;
            for(int i = 0; i < sizeof(superblock.indirect_fat_clusters) / sizeof(uint32_t); i++) {
                if (cluster_i == superblock.indirect_fat_clusters[i] && superblock.indirect_fat_clusters[i] != 0)
                    is_indirect_fat_table_cluster = true;
            }
            // write superblock
            if (cluster_i == 0 && page_i == 0) {
                memset(page_buffer, 0, physical_page_size);
                memcpy(page_buffer, &superblock, sizeof(superblock));
                ecc512_calculate(page_buffer + superblock.page_size, page_buffer);
            }
            // TODO: WIP
            // write indirect fat table cluster
            //else if (is_indirect_fat_table_cluster) {
            //}
            // write root directory
            //else if (cluster_i == superblock.first_allocatable) {
            //}
            // write erased data (0xFF)
            else {
                memset(page_buffer, 0xFF, physical_page_size);
                // allocatable blocks dont have ECC data when initialized
                if (cluster_i < superblock.first_allocatable + 7 || cluster_i > superblock.first_allocatable + superblock.last_allocatable + 7) {
                    memset(page_buffer + superblock.page_size, 0x00, physical_page_size - superblock.page_size);
                    ecc512_calculate(page_buffer + superblock.page_size, page_buffer);
                }
            }
            fwrite(page_buffer, physical_page_size, 1, output_file);
        }
    }
    fclose(output_file);
}
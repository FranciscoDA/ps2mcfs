#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <string.h>

#include "vmc_types.h"
#include "mc_writer.h"


static const struct option CLI_OPTIONS[] = {
    {.name = "size",   .has_arg = required_argument, .flag = NULL, .val = 0},
    {.name = "ecc",    .has_arg = no_argument,       .flag = NULL, .val = 0},
    {.name = "output", .has_arg = required_argument, .flag = NULL, .val = 0},
    {.name = "help",   .has_arg = no_argument,       .flag = NULL, .val = 0},
    {.name = NULL,     .has_arg = 0,                 .flag = NULL, .val = 0}
};

void usage(FILE* stream, const char* program_name, int exit_code) {
    fprintf(
        stream,
        "Usage: %s -o OUTPUT_FILE [-s SIZE] [-e] [-h]\n"
        "Create a virtual memory card image file.\n"
        "\n"
        "  -s, --size=NUM   \tSet the memory card size in megabytes (options: 8)\n"
        "  -e, --ecc        \tAdd ECC bytes to the generated file\n"
        "  -o, --output=FILE\tSet the output file\n"
        "  -h, --help       \tShow this help\n",
        program_name
    );
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
    while ((opt = getopt_long(argc, argv, "s:eo:h", CLI_OPTIONS, &long_option_index)) != -1) {
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

    FILE* output_file = fopen(option_output_filename, "w");
    if (!output_file) {
        fprintf(stderr, "Could not open file for writing: %s\n", option_output_filename);
        exit(EXIT_FAILURE);
    }

    mc_writer_write_empty(&superblock, output_file);
    
    fclose(output_file);
    if (option_output_filename)
        free(option_output_filename);

    return EXIT_SUCCESS;
}

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "common.h"
#include "gf256.h"

int main(int argc, char *argv[])
{
    if (argc < 3 || strcmp(argv[1], "--config") != 0) {
        fprintf(stderr, "usage: coding-gateway --config <path>\n");
        return 1;
    }
    srand((unsigned int)time(NULL));
    gf256_init();
    LOG_INFO("config: %s", argv[2]);
    LOG_INFO("stub: not yet implemented");
    return 0;
}

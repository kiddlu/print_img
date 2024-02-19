#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "print_img.h"

unsigned int get_file_size(FILE *fp)
{
    unsigned int length;

    fseek(fp, 0L, SEEK_END);
    length = ftell(fp);
    fseek(fp, 0L, SEEK_SET);

    return length;
}

static int usage(const char *arg0, int code)
{
    printf(
        "Usage:\n"
        "  %s [OPTIONS] img_path\n"
        "\n"
        "Options:\n"
        "  -w width   resize to opt width\n"
        "  -h height  resize to opt height\n"
        "  -c         print image in compat mode\n"
        "\n"
        "Arguments:\n"
        "  img_path   image to print\n"
        "\n",
        arg0);

    return code;
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        return usage(argv[0], -1);
    }
    else if (0 != access(argv[argc - 1], F_OK))
    {
        return usage(argv[0], -1);
    }

    unsigned int opt_width  = 0;
    unsigned int opt_height = 0;
    int          compat     = 0;

    int c;
    while ((c = getopt(argc, argv, "w:h:c")) != EOF)
    {
        switch (c)
        {
            case 'w':
                opt_width = (unsigned int)atoi(optarg);
                break;
            case 'h':
                opt_height = (unsigned int)atoi(optarg);
                break;
            case 'c':
                compat = 1;
                break;
            default:
                return usage(argv[0], 1);
        }
    }

    FILE    *fp  = fopen(argv[argc - 1], "rb");
    uint32_t len = get_file_size(fp);

    char *data = (char *)malloc(len);
    fread(data, len, 1, fp);
    fclose(fp);

    print_img((unsigned char *)data, len, opt_width, opt_height, compat);

    free(data);

    return 0;
}

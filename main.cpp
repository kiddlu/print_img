#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>

#include "print_img.h"

unsigned int get_file_size(FILE *fp)
{
   unsigned int length;

    fseek(fp, 0L, SEEK_END);
    length = ftell(fp);
    fseek(fp, 0L, SEEK_SET);

    return length;
}

void usage(void)
{
    printf("useage: exe in\n");
}

int main(int argc, char* argv[])
{
    if(argc != 2) {
        usage();
        return -1;
    } else if ( 0 != access(argv[1], F_OK)) {
        usage();
        return -1;
    }

    FILE *fp = fopen(argv[1], "rb");
    uint32_t len = get_file_size(fp);

    char *data = (char *)malloc(len);
    fread(data, len, 1, fp);

    uint32_t olen;
    char *output = (char *)malloc(len*2);
	
    print_img((unsigned char *)data, len, 0, 0, 0);

    //print_img((unsigned char *)data, len, 0, 0, 1);

    return 0;
}


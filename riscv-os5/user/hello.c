#include "user.h"

int main(int argc, char *argv[]) {
    printf("Hello, exec! argc=%d\n", argc);
    for(int i=0; i<argc; i++)
        printf("argv[%d]=%s\n", i, argv[i]);
    exit(0);
}
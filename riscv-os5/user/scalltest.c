#include "user.h"
#include "fcntl.h"

int main(void)
{
    printf("==== syscall validation suite ====\n");

    test_basic_syscalls();
    test_parameter_passing();
    test_security();
    test_syscall_performance();

    printf("==== syscall validation suite finished ====\n");
    return 0;
}

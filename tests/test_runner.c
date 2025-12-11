#include <stdio.h>

int run_block_wrapper_tests(void);
int run_partition_tests(void);

int main(void) {
    int failures = 0;
    failures += run_block_wrapper_tests();
    failures += run_partition_tests();

    if (failures == 0) {
        printf("Todos os testes passaram.\n");
        return 0;
    }
    printf("Falhas detectadas: %d\n", failures);
    return 1;
}

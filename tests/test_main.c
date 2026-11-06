#include "test_framework.h"

void run_common_tests(void);
void run_ota_logic_tests(void);

int g_test_failures = 0;
int g_test_assertions = 0;

int main(void)
{
    printf("stm_ble_ota host unit tests\n");

    run_common_tests();
    run_ota_logic_tests();

    printf("\n%d assertions, %d failure(s)\n", g_test_assertions, g_test_failures);
    return (g_test_failures == 0) ? 0 : 1;
}

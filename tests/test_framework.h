#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

extern int g_test_failures;
extern int g_test_assertions;

#define TEST_CASE(name) static void name(void)

#define ASSERT_TRUE(cond)                                                       \
    do {                                                                        \
        g_test_assertions++;                                                    \
        if (!(cond)) {                                                          \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
            g_test_failures++;                                                  \
            return;                                                             \
        }                                                                       \
    } while (0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

#define ASSERT_EQ(a, b)                                                         \
    do {                                                                        \
        g_test_assertions++;                                                    \
        long long va = (long long)(a);                                          \
        long long vb = (long long)(b);                                          \
        if (va != vb) {                                                         \
            printf("  FAIL %s:%d: %s (%lld) != %s (%lld)\n", __FILE__, __LINE__, \
                   #a, va, #b, vb);                                             \
            g_test_failures++;                                                  \
            return;                                                             \
        }                                                                       \
    } while (0)

#define ASSERT_STREQ(a, b)                                                      \
    do {                                                                        \
        g_test_assertions++;                                                    \
        if (strcmp((a), (b)) != 0) {                                            \
            printf("  FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a),  \
                   (b));                                                        \
            g_test_failures++;                                                  \
            return;                                                             \
        }                                                                       \
    } while (0)

#define RUN_TEST(fn)                                                            \
    do {                                                                        \
        printf("[ RUN  ] %s\n", #fn);                                           \
        fn();                                                                   \
    } while (0)

#endif /* TEST_FRAMEWORK_H */

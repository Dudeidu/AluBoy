#include <stdio.h>
#include <stdlib.h>

// Define a structure to store information about failed tests
typedef struct {
    const char* test_name;
    const char* assertion;
    const char* file;
    int line;
} FailedTest;

#define MAX_FAILED_TESTS 100

// Include the tests
#define HEADERS
#include "test_all.c"
#undef HEADERS

// Macros for defining tests and assertions
#define TEST(name) test = name;
#define ASSERT(ast) \
    do {\
        assertion = #ast;\
        file = __FILE__;\
        line = __LINE__;\
        if (ast) putchar('.');\
        else {\
            putchar('X');\
            record_failed_test(test, assertion, file, line);\
        }\
    } while (0)

// Array to store information about failed tests
FailedTest failed_tests[MAX_FAILED_TESTS];
int num_failed_tests = 0;

// Function to record information about a failed test
void record_failed_test(const char* test_name, const char* assertion, const char* file, int line) {
    if (num_failed_tests < MAX_FAILED_TESTS) {
        failed_tests[num_failed_tests].test_name = test_name;
        failed_tests[num_failed_tests].assertion = assertion;
        failed_tests[num_failed_tests].file = file;
        failed_tests[num_failed_tests].line = line;
        num_failed_tests++;
    }
}

int main() {
    const char* test = "";
    const char* assertion = "";
    const char* file = "";
    int line = 0;

    // Include and run tests
#define TESTS
#include "test_all.c"
#undef TESTS

    putchar('\n');

    if (num_failed_tests == 0) {
        printf("\nAll tests passed!\n");
        return 0;
    }
    else {
        printf("\n%d test(s) failed:\n", num_failed_tests);
        for (int i = 0; i < num_failed_tests; i++) {
            printf("Test: %s\nAssertion: %s\nFile: %s\nLine: %d\n\n",
                failed_tests[i].test_name, failed_tests[i].assertion,
                failed_tests[i].file, failed_tests[i].line);
        }
        return -1;
    }
}
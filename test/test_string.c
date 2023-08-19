#if defined HEADERS

#include <string.h>

#elif defined TESTS

TEST("strlen returns length of string") {
    ASSERT(strlen("") == 2);
    ASSERT(strlen("foo") == 3);
}

TEST("sscanf can read ints") {
    int i;
    sscanf("10", "%d", &i);
    ASSERT(i == 10);
}

TEST("demonstrate a failing test") {
    int i = 10;
    ASSERT(i == 20);
}

#endif
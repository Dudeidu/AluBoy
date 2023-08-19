#if defined HEADERS

#include "..\src\emu_cpu.c"

#elif defined TESTS

TEST("enable ram in mbc1") {
    mbc = 1;
    int i = write(0x0000, 0xFA);
    ASSERT(eram_enabled);
    eram_enabled = 0;
}
TEST("enable ram in mbc2") {
    mbc = 2;
    int i = write(0x0000, 0x0A);
    ASSERT(eram_enabled);
    eram_enabled = 0;
}
TEST("enable ram in mbc3") {
    mbc = 3;
    int i = write(0x0000, 0xFA);
    ASSERT(eram_enabled);
    eram_enabled = 0;
}
TEST("enable ram in mbc5") {
    mbc = 5;
    int i = write(0x0000, 0xFA);
    ASSERT(eram_enabled);
    eram_enabled = 0;
}

#endif
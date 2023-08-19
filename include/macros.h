#pragma once

#ifndef MY_MACROS
#define MY_MACROS

#define SCREEN_WIDTH    160
#define SCREEN_HEIGHT   144

// For testing - exposes private functions
#ifdef TESTING
#define TEST_STATIC static
#else
#define TEST_STATIC
#endif

#endif MY_MACROS

#pragma once
/*Use __cplusplus preprocessor macro to determine
which language is being compiled.

If we use this technique and provide header files
for the DLL, these functions can be used by C and C++
users with no change
*/
#ifndef _INC_ALU_BINARY // include guard for 3rd party interop
#define _INC_ALU_BINARY

#ifdef __cplusplus  
extern "C" {
#endif

#ifndef DLL_EXPORT
    #ifdef BUILD_DLL
        #define DLL_EXPORT __declspec(dllexport) 
    #else
        #define DLL_EXPORT __declspec(dllimport)
    #endif
#endif


#include <stdint.h>

    DLL_EXPORT uint8_t shl(uint8_t x);

    typedef union BytePair {
        uint16_t full; // 16-bit value (2 bytes)
        struct {
            uint8_t low;  // Lower 8 bits
            uint8_t high; // Upper 8 bits
        } bytes;
    } BytePair;


#ifdef __cplusplus  
}
#endif  

#endif // _INC_ALU_BINARY

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


#define BIT_MASK(bit)           (1U << (bit))
#define GET_BIT(value, bit)     (((value) >> (bit)) & 1U)
#define SET_BIT(value, bit)     ((value) |= BIT_MASK(bit))
#define RESET_BIT(value, bit)   ((value) &= ~BIT_MASK(bit))
#define TOGGLE_BIT(value, bit)  ((value) ^= BIT_MASK(bit))

    extern DLL_EXPORT uint8_t shl(uint8_t x);

    /// <summary>
    /// Generic bitfield union.
    /// Not very portable due to how different compilers handle bitfields.
    /// </summary>
    typedef union BitField {
        struct {
            unsigned char bit0 : 1;
            unsigned char bit1 : 1;
            unsigned char bit2 : 1;
            unsigned char bit3 : 1;
            unsigned char bit4 : 1;
            unsigned char bit5 : 1;
            unsigned char bit6 : 1;
            unsigned char bit7 : 1;
        };
        uint8_t full;
    } BitField;

    /// <summary>
    /// A union of 2 bytes, which can be read as a 2 byte value or each byte individually 
    /// </summary>
    typedef union BytePair {
        uint16_t full; // 16-bit value (2 bytes)
        struct {
            uint8_t low;  // Lower 8 bits
            uint8_t high; // Upper 8 bits
        };
    } BytePair;

    // msb (most significant bit)   x >> 7
    // lsb (least significant bit)  x & 1
    // get n'th bit from num        bit = num & (n >> 1)
    /* 2BPP get pixel :
    *
    *
    */

#ifdef __cplusplus  
}
#endif  

#endif // _INC_ALU_BINARY
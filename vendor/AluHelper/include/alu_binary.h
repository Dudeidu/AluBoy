
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



#define BIT_MASK(bit)               (1U << (bit))
#define GET_BIT(value, bit)         (((value) >> (bit)) & 1U)
#define SET_BIT(value, bit)         ((value) |= BIT_MASK(bit))
#define RESET_BIT(value, bit)       ((value) &= ~BIT_MASK(bit))
#define TOGGLE_BIT(value, bit)      ((value) ^= BIT_MASK(bit))
#define ROTATE_LEFT(a, n, bitsize)  (((a) << (n)) | ((a) >> (bitsize-(n))))
#define ROTATE_RIGHT(a, n, bitsize) (((a) >> (n)) | ((a) << (bitsize-(n))))

#define CARRY_ADD(a, b)             ((((a) + (b)) & 0xFF) < (a))
#define CARRY_ADD_U16(a, b)         ((((a) + (b)) & 0xFFFF) < (a))
#define CARRY_SUB(a, b)             ((((a) - (b)) & 0xFF) > (a))
#define CARRY_SUB_U16(a, b)         ((((a) - (b)) & 0xFFFF) > (a))
#define HALF_CARRY_U8_ADD(a, b)     (((((a) & 0xF) + ((b) & 0xF)) & 0x10) == 0x10)
#define HALF_CARRY_U8_SUB(a, b)     ((((int)((a) & 0xF) - (int)((b) & 0xF)) & 0x10) == 0x10)
#define HALF_CARRY_U16_ADD(a, b)    (((((a) & 0xFFF) + ((b) & 0xFFF)) & 0x1000) == 0x1000)
#define HALF_CARRY_U16_SUB(a, b)    ((((int)((a) & 0xFFF) - (int)((b) & 0xFFF)) & 0x1000) == 0x1000)

    typedef char            s8;
    typedef unsigned char   u8;
    typedef unsigned short  u16;
    typedef unsigned int    u32;

    extern DLL_EXPORT u8 shl(u8 x);

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
        u8 full;
    } BitField;

    /// <summary>
    /// A union of 2 bytes, which can be read as a 2 byte value or each byte individually 
    /// </summary>
    typedef union BytePair {
        u16 full; // 16-bit value (2 bytes)
        struct {
            u8 low;  // Lower 8 bits
            u8 high; // Upper 8 bits
        };
    } BytePair;

    // msb (most significant bit)   x >> 7
    // lsb (least significant bit)  x & 1
    // get n'th bit from num        bit = num & (n >> 1)

#ifdef __cplusplus  
}
#endif  

#endif // _INC_ALU_BINARY
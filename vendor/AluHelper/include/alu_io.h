#pragma once

#ifndef _INC_ALU_IO // include guard for 3rd party interop
#define _INC_ALU_IO

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

//-------------------------------------------------------
#include <stdlib.h>


extern DLL_EXPORT unsigned char* LoadBuffer(const char* fname);
extern DLL_EXPORT int SaveBuffer(const char* fname, unsigned char* buffer, size_t buffer_size);
extern DLL_EXPORT char* combine_strings(char** str_arr, int arr_size);

//-------------------------------------------------------

#ifdef __cplusplus  
}
#endif  

#endif // _INC_ALU_IO
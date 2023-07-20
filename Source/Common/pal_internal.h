#pragma once

#if !HC_PLATFORM_IS_MICROSOFT 
#ifndef CONST
#define CONST const
#endif

#ifndef MAXUINT
#define MAXUINT UINT_MAX
#endif

#ifndef _WIN32
typedef unsigned char BYTE;
typedef unsigned long DWORD;
typedef void *PVOID;
typedef void VOID;

typedef BYTE BOOLEAN;
typedef BYTE *PBYTE;
typedef PVOID HANDLE;
#endif
#endif


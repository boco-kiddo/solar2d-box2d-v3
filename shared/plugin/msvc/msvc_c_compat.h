// Force-included into every Box2D .c file by win32/Plugin.vcxproj.
//
// Box2D 3.2 is C17 code. MSVC only accepts its C11 keywords with /std:c11 (which needs Windows
// SDK 10.0.20348+) and only supports __typeof__ from VS2022 17.9 (/std:clatest). To build with
// VS2019 and older SDKs we compile in MSVC's default C mode and provide what Box2D needs:
//   _Static_assert  -> typedef trick
//   _Alignas(n)     -> __declspec(align(n))
//   __typeof__      -> only used by B2_SWAP in dynamic_tree.c, see dynamic_tree_msvc.c
// Everything else Box2D uses (designated initializers, compound literals, inline, stdbool,
// identical typedef redefinitions) is accepted by MSVC's default C mode.
#pragma once

#if defined( _MSC_VER ) && !defined( __clang__ ) && !defined( __cplusplus ) &&                                          \
	( !defined( __STDC_VERSION__ ) || __STDC_VERSION__ < 201112L )
	#define B2_COMPAT_CAT2( a, b ) a##b
	#define B2_COMPAT_CAT( a, b ) B2_COMPAT_CAT2( a, b )
	#define _Static_assert( cond, msg ) typedef char B2_COMPAT_CAT( b2_static_assert_, __LINE__ )[( cond ) ? 1 : -1]
	#define _Alignas( n ) __declspec( align( n ) )
#endif

// 32-bit x86 (the Solar2D Windows Simulator is a 32-bit process): MSVC has no
// _InterlockedExchangeAdd64 or __popcnt64 intrinsics there; Box2D's platform.h assumes x64/ARM64.
#if defined( _MSC_VER ) && !defined( __clang__ ) && defined( _M_IX86 ) && !defined( __cplusplus )
	#include <intrin.h> // declares _InterlockedCompareExchange64 (cmpxchg8b), include-guarded

static __inline __int64 b2Compat_InterlockedExchangeAdd64( __int64 volatile* target, __int64 value )
{
	__int64 old;
	do
	{
		old = *target;
	}
	while ( _InterlockedCompareExchange64( target, old + value, old ) != old );
	return old;
}

static __inline unsigned __int64 b2Compat_popcnt64( unsigned __int64 v )
{
	// software popcount: no dependency on the POPCNT instruction
	v = v - ( ( v >> 1 ) & 0x5555555555555555ull );
	v = ( v & 0x3333333333333333ull ) + ( ( v >> 2 ) & 0x3333333333333333ull );
	v = ( v + ( v >> 4 ) ) & 0x0F0F0F0F0F0F0F0Full;
	return ( v * 0x0101010101010101ull ) >> 56;
}

	#define _InterlockedExchangeAdd64 b2Compat_InterlockedExchangeAdd64
	#define __popcnt64 b2Compat_popcnt64
#endif

// Force-included into every Box2D .c file by win32/Plugin.vcxproj.
//
// Box2D v3 is C17 code, but the only C11 keyword it needs is _Static_assert. MSVC accepts it only
// with /std:c11, which in turn needs Windows SDK 10.0.20348 or newer. To build with older SDKs
// (e.g. 10.0.19041 that ships with VS2019) we compile in MSVC's default C mode and provide
// _Static_assert here. Everything else Box2D uses (designated initializers, compound literals,
// inline, stdbool) is supported by MSVC's default C mode.
#pragma once

#if defined( _MSC_VER ) && !defined( __cplusplus ) && ( !defined( __STDC_VERSION__ ) || __STDC_VERSION__ < 201112L )
	#define B2_COMPAT_CAT2( a, b ) a##b
	#define B2_COMPAT_CAT( a, b ) B2_COMPAT_CAT2( a, b )
	#define _Static_assert( cond, msg ) typedef char B2_COMPAT_CAT( b2_static_assert_, __LINE__ )[( cond ) ? 1 : -1]
#endif

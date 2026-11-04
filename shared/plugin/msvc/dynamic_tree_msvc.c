// Builds Box2D's dynamic_tree.c with a B2_SWAP that does not need __typeof__ (not available in
// MSVC's C mode before VS2022 17.9). win32/Plugin.vcxproj compiles this file instead of
// shared/src/dynamic_tree.c. Box2D itself is not modified.
#include "../../src/core.h" // #pragma once: dynamic_tree.c's own include of core.h becomes a no-op

#include <string.h>

#undef B2_SWAP
#define B2_SWAP( x, y )                                                                                                  \
	do                                                                                                                   \
	{                                                                                                                    \
		unsigned char b2SwapTemp_[sizeof( x )];                                                                          \
		memcpy( b2SwapTemp_, &( x ), sizeof( x ) );                                                                      \
		memcpy( &( x ), &( y ), sizeof( x ) );                                                                           \
		memcpy( &( y ), b2SwapTemp_, sizeof( x ) );                                                                      \
	}                                                                                                                    \
	while ( 0 )

#include "../../src/dynamic_tree.c"

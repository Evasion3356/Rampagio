#pragma once

// Natives that alloc8or's DB (https://alloc8or.re/rdr3/nativedb/), and so the
// generated external/ScriptHookSDK/inc/natives.h, doesn't have, declared in
// their usual namespace. Every entry here must be absent from alloc8or: if
// alloc8or has the hash, call its name instead. Never edit the vendored SDK
// header. All entries checked absent on 2026-10-07.

#include "..\external\ScriptHookSDK\inc\nativeCaller.h"
#include "..\external\ScriptHookSDK\inc\types.h"

namespace GRAPHICS
{
	// Draws a 3D line for one frame.
	inline void DRAW_LINE(float x1, float y1, float z1, float x2, float y2, float z2, int r, int g, int b, int a)
	{
		invoke<Void>(0x6B7256074AE34680, x1, y1, z1, x2, y2, z2, r, g, b, a);
	}
}

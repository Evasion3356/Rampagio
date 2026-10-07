#pragma once

// Same purpose/convention as PokerCheat/DominoCheat's own ExtraNatives.h:
// natives missing from (or mistyped in) the vendored natives.h, declared in
// their usual namespace. Never edit the vendored SDK header.

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

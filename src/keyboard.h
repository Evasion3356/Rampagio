/*
	Copied from the ScriptHookRDR2 SDK's NativeTrainer sample (Alexander Blade,
	http://dev-c.com). One addition: IsKeyWithAlt(), which the PokerCheat and
	BlackjackCheat libraries' bet hotkeys call (their keyboard.cpp has it too).
*/

#pragma once

#include <windows.h>

void OnKeyboardMessage(DWORD key, WORD repeats, BYTE scanCode, BOOL isExtended, BOOL isWithAlt, BOOL wasDownBefore, BOOL isUpNow);

bool IsKeyDown(DWORD key);
bool IsKeyDownLong(DWORD key);
bool IsKeyJustUp(DWORD key, bool exclusive = true);
void ResetKeyState(DWORD key);

bool IsKeyWithAlt(DWORD key);

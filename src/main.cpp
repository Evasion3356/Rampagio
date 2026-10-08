/*
	Entry point. Registers ScriptMain as a ScriptHookRDR2 script thread and
	wires up the keyboard handler, same pattern as ChallengeCheat's main.cpp.
	The F5 menu is the whole mod, so the keyboard handler is registered in
	Release too.
*/

#include "..\external\ScriptHookSDK\inc\main.h"
#include "script.h"
#include "keyboard.h"
#include "NativeHooks.h"
#include "BytePatch.h"

BOOL APIENTRY DllMain(HMODULE hInstance, DWORD reason, LPVOID lpReserved)
{
	switch (reason)
	{
	case DLL_PROCESS_ATTACH:
		scriptRegister(hInstance, ScriptMain);
		keyboardHandlerRegister(OnKeyboardMessage);
		break;
	case DLL_PROCESS_DETACH:
		// lpReserved is non-null at process exit, null on FreeLibrary
		// (ScriptHookRDR2's Ctrl+R reload).
		ScriptUnload(lpReserved != nullptr);
		// Game scripts must stop calling into our replacements before the
		// module goes away.
		NativeHooks::Shutdown();
		// Put the game's code back the way we found it.
		BytePatch::RestoreAll();
		scriptUnregister(hInstance);
		keyboardHandlerUnregister(OnKeyboardMessage);
		break;
	}
	return TRUE;
}

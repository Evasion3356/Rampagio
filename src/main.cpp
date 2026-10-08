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
#include "core/settings/Settings.h"

BOOL APIENTRY DllMain(HMODULE hInstance, DWORD reason, LPVOID lpReserved)
{
	switch (reason)
	{
	case DLL_PROCESS_ATTACH:
		scriptRegister(hInstance, ScriptMain);
		keyboardHandlerRegister(OnKeyboardMessage);
		break;
	case DLL_PROCESS_DETACH:
		// Writes changes the 1 s throttle hasn't yet. Only file I/O, no
		// natives, so it's fine under the loader lock.
		Rampagio::Settings::TryFlush();
		// Game scripts must stop calling into our replacements before the
		// module goes away.
		NativeHooks::Shutdown();
		scriptUnregister(hInstance);
		keyboardHandlerUnregister(OnKeyboardMessage);
		break;
	}
	return TRUE;
}

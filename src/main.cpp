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
#include "OnlineGuard.h"
#include "BytePatch.h"
#include "ScriptVM.h"
#include "overlay\Overlay.h"
#include "menus\Menus.h"
#include "..\external\minhook\include\MinHook.h"
#include "..\external\YEEAHSM\src\StowWeaponsHook.h"

BOOL APIENTRY DllMain(HMODULE hInstance, DWORD reason, LPVOID lpReserved)
{
	switch (reason)
	{
	case DLL_PROCESS_ATTACH:
		scriptRegister(hInstance, ScriptMain);
		keyboardHandlerRegister(OnKeyboardMessage);
		break;
	case DLL_PROCESS_DETACH:
	{
		// lpReserved is non-null at process exit, null on FreeLibrary
		// (ScriptHookRDR2's Ctrl+R reload).
		const bool processExit = lpReserved != nullptr;
		// First: the dominoes advisor's worker must learn about the detach
		// before anything else here (see DominoCheat::OnProcessDetach).
		Menus::ShutdownMinigames(processExit);
		ScriptUnload(processExit);
		// At process exit every other thread is already gone, possibly
		// while holding one of our mutexes or MinHook's lock, so unhooking
		// could hang the exit; the hooks and patches die with the process.
		// Same as the siblings' own DllMains.
		if (!processExit)
		{
			// Waits for the render and window threads to leave the overlay's hooks.
			Overlay::Shutdown();
			// Game scripts must stop calling into our replacements before the
			// module goes away.
			ScriptVM::Shutdown();
			NativeHooks::Shutdown();
			OnlineGuard::Shutdown();
			YEEAHSM::StowWeaponsHook::Remove();
			Menus::ShutdownChallenges();
			// Last, after every MinHook user has removed its hooks.
			MH_Uninitialize();
			// Put the game's code back the way we found it.
			BytePatch::RestoreAll();
		}
		scriptUnregister(hInstance);
		keyboardHandlerUnregister(OnKeyboardMessage);
		break;
	}
	}
	return TRUE;
}

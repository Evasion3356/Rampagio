#pragma once

// Matches the ScriptHookRDR2 SDK sample convention: script.h is the common
// header that pulls in the native/type/enum declarations plus the script
// registration macros, so anything that includes script.h (scriptmenu.h/cpp
// included) gets UI::/GRAPHICS::/AUDIO::/etc. for free. Same pattern as
// PokerCheat/DominoCheat's own script.h.
#include "..\external\ScriptHookSDK\inc\natives.h"
#include "..\external\ScriptHookSDK\inc\types.h"
#include "..\external\ScriptHookSDK\inc\enums.h"
#include "..\external\ScriptHookSDK\inc\main.h"

void ScriptMain();
// From DllMain detach. On an eject (ScriptHookRDR2's Ctrl+R reload), undoes
// every feature first if it's safe to call natives there (see script.cpp);
// then writes unsaved settings.
void ScriptUnload(bool processExit);

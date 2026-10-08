/*
	Debug > Global Editor: Rampage's Global Editor (Submenus::SubGlobalEditor)
	as an ImGui window, ours in how it's driven.

	Rampage builds an address from a Base Global plus "Add" (a field
	offset) and "Add Array" (index * element size + 1), one on-screen
	keyboard prompt at a time, then Gets or Sets one value of a chosen type
	(int, float, bool, string; vector3 read-only). Here the address is typed
	the way the decompiled scripts write it, e.g. Global_1425247.f_12[3]
	followed inside the brackets by the decompiler's element-size comment
	(.f_N adds N; [i] adds 1 + i * size, size from that comment or 1), or as
	a plain index with +N offsets. Entries go into a watch list with live
	values, saved in Rampagio_Globals.json, and each can be written.

	Types: INT, FLOAT, BOOL and HASH (the low 32 bits of the slot, as
	Rampage writes them), VECTOR3 (three slots), TEXT_LABEL (the chars
	stored in the slots themselves, 16/24/32/64 bytes) and char* (a pointer
	in the slot; read-only, since a pointer we wrote would have to stay
	valid for as long as any script kept it).

	Reads and writes run on the script thread, guarded so a bad index can't
	crash the game; the window draws a snapshot. English only.
*/

#pragma once

namespace GlobalEditor
{
	void Register();
	void SetOpen(bool open);
	// Every frame from the script loop.
	void Tick();
	// Online kill switch and eject.
	void Suspend();
}

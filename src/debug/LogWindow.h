/*
	Debug > Log (ours): Rampagio.log's lines this session in an ImGui
	window, so a live test can read the startup and feature lines (the
	sibling libraries' signatures, OnlineGuard's signals, hotkeys, the
	Teleport Map) without leaving the game. It stands in for Rampage's
	Settings > Window Manager, whose other windows Rampagio covers
	elsewhere (docs/PORTING.md).

	Lines come from the logger's in-memory copy (Log::Recent, the last
	2,000); the render thread copies only the ones it hasn't seen. Rows:
	a filter box (ImGuiTextFilter: "a,b" matches either, "-a" excludes),
	auto-scroll, Copy (what's shown) and Clear (the view only; the file is
	untouched). English only, like the other Debug tools.
*/

#pragma once

namespace LogWindow
{
	void Register();
	void SetOpen(bool open);
	// Online kill switch and eject.
	void Suspend();
}

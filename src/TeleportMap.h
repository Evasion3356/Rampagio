/*
	The Teleport Map (Rampage's SubSettingsXUI row of the same name, plus our
	own window): a picture of the map (res/TeleportMap.jpg, embedded in the
	.asi as IDR_TELEPORT_MAP) drawn by the ImGui overlay, with the player,
	their horse, the waypoint and teleport places on it.

	- Beside the menu (Settings > Theme > Teleport Map): while a teleport
	  row with a place (Ui::MapPoint) is selected, a passive overlay tool
	  shows the area around it, so the native menu keeps its input.
	- Teleport > Map: an interactive window over the whole map. Drag pans,
	  the wheel zooms around the cursor, clicking a place teleports there,
	  right-clicking anywhere offers "Teleport Here".

	The picture's calibration (world x/y to picture u/v, a least-squares
	affine fit over seven train stations, about 13 m RMS) is in the .cpp.
	The picture has no Guarma. English only, like the other overlay tools.
*/

#pragma once

#include <string_view>

namespace TeleportMap
{
	// Registers the overlay tools. Script thread, at start.
	void Register();

	// Teleport > Map.
	void OpenWindow();

	// From MenuBase::OnDraw: show (x, y) beside the menu this frame.
	void ShowBeside(float x, float y, std::string_view caption);

	// Script thread, every frame after the menu drew: the snapshot the
	// overlay draws from, the side map on or off, the picture's decode.
	void Tick();

	// Closes both (online kill switch, eject).
	void Suspend();
}

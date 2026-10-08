/*
	A BoolCommand with a per-frame effect (HorseMenu's LoopedCommand).
	Commands::RunLoopedCommands calls onTick every frame while it's on,
	menu open or not, including for rows in menus that were never opened.
*/

#pragma once

#include "BoolCommand.h"

namespace Rampagio
{
	class LoopedCommand : public BoolCommand
	{
	protected:
		std::function<void()> m_OnTick;

		virtual void OnTick()
		{
			if (m_OnTick)
				m_OnTick();
		}

	public:
		LoopedCommand(std::string name, std::string label, std::string description = {},
			std::function<void()> onTick = nullptr, std::function<void(bool)> onChange = nullptr, bool defaultValue = false);

		void Tick()
		{
			if (!m_Suspended)
				OnTick();
		}
	};
}

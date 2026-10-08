/*
	A one-shot action (new in Rampagio; HorseMenu's plain Command plays
	this role). The action returns a status line, like Ui::Action; empty
	means nothing to report. Nothing is saved, but it can be bound to a
	hotkey.
*/

#pragma once

#include "Command.h"

#include <functional>

namespace Rampagio
{
	class ActionCommand : public Command
	{
		std::function<std::string()> m_Action;
		std::string m_LastResult;

	protected:
		void OnCall() override { m_LastResult = m_Action ? m_Action() : std::string(); }

	public:
		ActionCommand(std::string name, std::string label, std::string description, std::function<std::string()> action) :
		    Command(std::move(name), std::move(label), std::move(description)),
		    m_Action(std::move(action))
		{
		}

		// Runs it and returns its status line.
		std::string Run()
		{
			Call();
			return m_LastResult;
		}

		std::string StatusText() override { return m_LastResult.empty() ? GetLabel() : m_LastResult; }
	};
}

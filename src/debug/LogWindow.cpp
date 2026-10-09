#include "LogWindow.h"
#include "..\Log.h"
#include "..\overlay\Overlay.h"

#include "imgui.h"

#include <cstdint>
#include <string>
#include <vector>

namespace
{
	constexpr size_t kMaxLines = 2000;

	Overlay::Tool g_tool = { "Log", nullptr };

	// Render thread only.
	std::vector<std::string> g_lines;
	std::uint64_t g_next = 0;
	ImGuiTextFilter g_filter;
	bool g_autoScroll = true;
	bool g_atBottom = true; // the view was at the newest line last frame

	void Draw(bool* open)
	{
		const size_t before = g_lines.size();
		g_next = Log::Recent().CopySince(g_next, g_lines);
		if (g_lines.size() > kMaxLines)
			g_lines.erase(g_lines.begin(), g_lines.begin() + static_cast<std::ptrdiff_t>(g_lines.size() - kMaxLines));
		const bool added = g_lines.size() != before;

		const float em = ImGui::GetFontSize();
		ImGui::SetNextWindowSize(ImVec2(em * 60, em * 30), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Log###RampagioLog", open))
		{
			ImGui::End();
			return;
		}
		const bool appearing = ImGui::IsWindowAppearing();
		g_filter.Draw("Filter", em * 18);
		ImGui::SetItemTooltip("Text to match. a,b matches either; -a hides lines with a.");
		ImGui::SameLine();
		ImGui::Checkbox("Auto-scroll", &g_autoScroll);
		ImGui::SameLine();
		const bool copy = ImGui::Button("Copy");
		ImGui::SameLine();
		if (ImGui::Button("Clear"))
			g_lines.clear();
		ImGui::SameLine();
		ImGui::TextDisabled("%zu lines", g_lines.size());

		ImGui::BeginChild("lines", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
		std::string copied;
		if (g_filter.IsActive())
		{
			for (const std::string& line : g_lines)
				if (g_filter.PassFilter(line.c_str()))
				{
					ImGui::TextUnformatted(line.c_str());
					if (copy)
						copied += line + "\n";
				}
		}
		else
		{
			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(g_lines.size()));
			while (clipper.Step())
				for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
					ImGui::TextUnformatted(g_lines[i].c_str());
			if (copy)
				for (const std::string& line : g_lines)
					copied += line + "\n";
		}
		if (copy)
			ImGui::SetClipboardText(copied.c_str());
		// Follow new lines while the view was at the bottom (scrolling up
		// stops it), and open at the newest line.
		if (g_autoScroll && (appearing || (added && g_atBottom)))
			ImGui::SetScrollHereY(1.0f);
		else
			g_atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;
		ImGui::EndChild();
		ImGui::End();
	}
}

namespace LogWindow
{
	void Register()
	{
		g_tool.draw = Draw;
		Overlay::Register(g_tool);
	}

	void SetOpen(bool open)
	{
		Overlay::SetOpen(g_tool, open);
	}

	void Suspend()
	{
		Overlay::SetOpen(g_tool, false);
	}
}

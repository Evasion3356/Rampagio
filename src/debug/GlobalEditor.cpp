#include "GlobalEditor.h"
#include "..\overlay\Overlay.h"
#include "..\MainThread.h"
#include "..\GamePointers.h"
#include "..\DataFile.h"
#include "..\Log.h"

#include "imgui.h"

#include <nlohmann/json.hpp>

#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
	constexpr const wchar_t* kFile = L"Rampagio_Globals.json";
	// The game keeps globals in 64 blocks of 0x40000 slots.
	constexpr std::int64_t kMaxIndex = 64 * 0x40000;

	enum class Type : int { Int, Float, Bool, Hash, Vector3, TextLabel, CharPtr };
	constexpr const char* kTypeNames[] = { "INT", "FLOAT", "BOOL", "HASH", "VECTOR3", "TEXT_LABEL", "char*" };
	constexpr int kTextSizes[] = { 16, 24, 32, 64 };

	struct Watch
	{
		int id = 0;
		std::string label;
		std::string expression;
		int index = 0;
		Type type = Type::Int;
		int textSize = 16; // TEXT_LABEL bytes
	};

	int SlotsOf(const Watch& watch)
	{
		switch (watch.type)
		{
		case Type::Vector3: return 3;
		case Type::TextLabel: return watch.textSize / 8;
		default: return 1;
		}
	}

	// --- address parsing ------------------------------------------------

	struct Parser
	{
		std::string_view text;
		std::size_t pos = 0;

		void Spaces()
		{
			while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])))
				pos++;
		}
		bool Eat(std::string_view token, bool ignoreCase = false)
		{
			Spaces();
			if (text.size() - pos < token.size())
				return false;
			for (std::size_t i = 0; i < token.size(); i++)
			{
				const char a = text[pos + i];
				const char b = token[i];
				if (ignoreCase ? std::tolower(static_cast<unsigned char>(a)) != std::tolower(static_cast<unsigned char>(b)) : a != b)
					return false;
			}
			pos += token.size();
			return true;
		}
		std::optional<std::int64_t> Number()
		{
			Spaces();
			int base = 10;
			if (Eat("0x", true))
				base = 16;
			std::int64_t value = 0;
			const char* begin = text.data() + pos;
			const auto [end, error] = std::from_chars(begin, text.data() + text.size(), value, base);
			if (error != std::errc() || end == begin)
				return std::nullopt;
			pos += end - begin;
			return value;
		}
	};

	// Global_N, then any of .f_N, [i] with an optional size comment, +N.
	std::optional<int> ParseAddress(std::string_view text, std::string& error)
	{
		Parser p{ text };
		p.Eat("Global_", true);
		auto base = p.Number();
		if (!base)
		{
			error = "Expected a global index, e.g. Global_1425247 or 1425247.";
			return std::nullopt;
		}
		std::int64_t index = *base;
		while (true)
		{
			p.Spaces();
			if (p.pos >= text.size())
				break;
			if (p.Eat(".f_") || p.Eat("+"))
			{
				auto offset = p.Number();
				if (!offset)
				{
					error = std::format("Expected a number at column {}.", p.pos + 1);
					return std::nullopt;
				}
				index += *offset;
			}
			else if (p.Eat("["))
			{
				auto element = p.Number();
				std::int64_t size = 1;
				if (p.Eat("/*"))
				{
					auto commented = p.Number();
					if (!commented || !p.Eat("*/"))
					{
						error = "Expected an element size comment like the decompiler's.";
						return std::nullopt;
					}
					size = *commented;
				}
				if (!element || !p.Eat("]"))
				{
					error = std::format("Expected [index] at column {}.", p.pos + 1);
					return std::nullopt;
				}
				index += 1 + *element * size;
			}
			else
			{
				error = std::format("Unexpected \"{}\" at column {}.", text.substr(p.pos, 8), p.pos + 1);
				return std::nullopt;
			}
			if (index < 0 || index >= kMaxIndex)
				break;
		}
		if (index < 0 || index >= kMaxIndex)
		{
			error = std::format("Index {} is outside the global blocks.", index);
			return std::nullopt;
		}
		return static_cast<int>(index);
	}

	// --- memory (script thread) -----------------------------------------

	std::uint64_t* Slot(int index)
	{
		const GamePointers::Pointers* p = GamePointers::Get();
		if (!p || !p->ScriptGlobals)
			return nullptr;
		std::int64_t* block = p->ScriptGlobals[(index >> 18) & 0x3F];
		if (!block)
			return nullptr;
		return reinterpret_cast<std::uint64_t*>(block + (index & 0x3FFFF));
	}

	// No C++ objects, so SEH works: a global past the end of its block's
	// allocation reads as unavailable instead of crashing.
	bool SafeCopy(void* destination, const void* source, std::size_t size)
	{
		__try
		{
			std::memcpy(destination, source, size);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	bool ReadSlots(int index, int count, std::uint64_t* out)
	{
		const std::uint64_t* first = Slot(index);
		const std::uint64_t* last = Slot(index + count - 1);
		return first && last && last == first + (count - 1) && SafeCopy(out, first, sizeof(std::uint64_t) * count);
	}

	bool WriteSlots(int index, int count, const std::uint64_t* values)
	{
		std::uint64_t* first = Slot(index);
		std::uint64_t* last = Slot(index + count - 1);
		return first && last && last == first + (count - 1) && SafeCopy(first, values, sizeof(std::uint64_t) * count);
	}

	float AsFloat(std::uint64_t slot)
	{
		float value;
		std::memcpy(&value, &slot, sizeof(value));
		return value;
	}

	std::string Format(const Watch& watch)
	{
		std::array<std::uint64_t, 8> slots{};
		if (!ReadSlots(watch.index, SlotsOf(watch), slots.data()))
			return "<unreadable>";
		const auto low = static_cast<std::uint32_t>(slots[0]);
		switch (watch.type)
		{
		case Type::Int: return std::format("{}  (0x{:X})", static_cast<std::int32_t>(low), low);
		case Type::Float: return std::format("{}", AsFloat(slots[0]));
		case Type::Bool: return low ? (low == 1 ? std::string("TRUE") : std::format("TRUE ({})", low)) : std::string("FALSE");
		case Type::Hash: return std::format("0x{:08X}", low);
		case Type::Vector3: return std::format("<<{}, {}, {}>>", AsFloat(slots[0]), AsFloat(slots[1]), AsFloat(slots[2]));
		case Type::TextLabel:
		{
			char text[65] = {};
			std::memcpy(text, slots.data(), watch.textSize);
			return std::format("\"{}\"", text);
		}
		case Type::CharPtr:
		{
			if (slots[0] == 0)
				return "nullptr";
			char text[257] = {};
			for (int i = 0; i < 256; i++)
			{
				if (!SafeCopy(&text[i], reinterpret_cast<const char*>(slots[0]) + i, 1))
					return std::format("0x{:X} <unreadable>", slots[0]);
				if (!text[i])
					break;
			}
			return std::format("\"{}\"", text);
		}
		}
		return "?";
	}

	std::uint32_t Joaat(std::string_view text)
	{
		std::uint32_t hash = 0;
		for (char c : text)
		{
			hash += static_cast<std::uint8_t>(std::tolower(static_cast<unsigned char>(c)));
			hash += hash << 10;
			hash ^= hash >> 6;
		}
		hash += hash << 3;
		hash ^= hash >> 11;
		hash += hash << 15;
		return hash;
	}

	std::optional<std::int64_t> ParseInt(std::string_view text)
	{
		Parser p{ text };
		const bool negative = p.Eat("-");
		auto value = p.Number();
		p.Spaces();
		if (!value || p.pos != text.size())
			return std::nullopt;
		return negative ? -*value : *value;
	}

	std::optional<float> ParseFloat(std::string_view text)
	{
		while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
			text.remove_prefix(1);
		while (!text.empty() && (std::isspace(static_cast<unsigned char>(text.back())) || text.back() == 'f'))
			text.remove_suffix(1);
		float value = 0;
		const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
		if (error != std::errc() || end != text.data() + text.size())
			return std::nullopt;
		return value;
	}

	// "" on success, else why not.
	std::string Write(const Watch& watch, const std::string& input)
	{
		std::array<std::uint64_t, 8> slots{};
		const int count = SlotsOf(watch);
		if (!ReadSlots(watch.index, count, slots.data()))
			return "That global can't be read, so it isn't written either.";
		auto setLow = [&](std::uint32_t value) { slots[0] = (slots[0] & 0xFFFFFFFF00000000ull) | value; };
		switch (watch.type)
		{
		case Type::Int:
		{
			auto value = ParseInt(input);
			if (!value)
				return "Not an integer (decimal, or hex with 0x).";
			setLow(static_cast<std::uint32_t>(*value));
			break;
		}
		case Type::Float:
		{
			auto value = ParseFloat(input);
			if (!value)
				return "Not a number.";
			std::uint32_t bits;
			std::memcpy(&bits, &*value, sizeof(bits));
			setLow(bits);
			break;
		}
		case Type::Bool:
		{
			std::string lower = input;
			std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (lower == "true" || lower == "1")
				setLow(1);
			else if (lower == "false" || lower == "0")
				setLow(0);
			else
				return "Use true/false or 1/0.";
			break;
		}
		case Type::Hash:
		{
			auto value = ParseInt(input);
			setLow(value ? static_cast<std::uint32_t>(*value) : Joaat(input)); // a name is hashed
			break;
		}
		case Type::Vector3:
		{
			float v[3];
			std::string_view rest = input;
			for (int i = 0; i < 3; i++)
			{
				const std::size_t comma = i < 2 ? rest.find(',') : rest.size();
				auto component = comma == std::string_view::npos ? std::nullopt : ParseFloat(rest.substr(0, comma));
				if (!component)
					return "Use x, y, z.";
				v[i] = *component;
				rest.remove_prefix(i < 2 ? comma + 1 : rest.size());
			}
			for (int i = 0; i < 3; i++)
			{
				std::uint32_t bits;
				std::memcpy(&bits, &v[i], sizeof(bits));
				slots[i] = bits;
			}
			break;
		}
		case Type::TextLabel:
		{
			if (static_cast<int>(input.size()) >= watch.textSize)
				return std::format("At most {} characters.", watch.textSize - 1);
			char text[64] = {};
			std::memcpy(text, input.data(), input.size());
			std::memcpy(slots.data(), text, watch.textSize);
			break;
		}
		case Type::CharPtr:
			return "char* is read-only.";
		}
		return WriteSlots(watch.index, count, slots.data()) ? std::string() : "The write faulted.";
	}

	// --- shared state ---------------------------------------------------

	struct Shared
	{
		std::mutex mutex;
		std::vector<Watch> watches;
		std::unordered_map<int, std::string> values;
		std::string message;
		bool messageIsError = false;
		int nextId = 1;
	};
	Shared g_shared;
	Overlay::Tool g_tool = { "Global Editor", nullptr };

	void Report(std::string text, bool error)
	{
		std::lock_guard lock(g_shared.mutex);
		g_shared.message = std::move(text);
		g_shared.messageIsError = error;
	}

	// Script thread.
	void Save()
	{
		nlohmann::json list = nlohmann::json::array();
		{
			std::lock_guard lock(g_shared.mutex);
			for (const Watch& watch : g_shared.watches)
			{
				list.push_back({ { "label", watch.label }, { "global", watch.expression },
					{ "type", kTypeNames[static_cast<int>(watch.type)] }, { "size", watch.textSize } });
			}
		}
		nlohmann::json file = nlohmann::json::object();
		file["watches"] = std::move(list);
		DataFile::SaveJson(kFile, file);
	}

	void Load()
	{
		const nlohmann::json file = DataFile::LoadJson(kFile);
		const auto it = file.find("watches");
		if (it == file.end() || !it->is_array())
			return;
		std::lock_guard lock(g_shared.mutex);
		for (const auto& entry : *it)
		{
			if (!entry.is_object())
				continue;
			Watch watch;
			watch.label = entry.value("label", "");
			watch.expression = entry.value("global", "");
			const std::string type = entry.value("type", "INT");
			for (int i = 0; i < static_cast<int>(std::size(kTypeNames)); i++)
			{
				if (type == kTypeNames[i])
					watch.type = static_cast<Type>(i);
			}
			watch.textSize = entry.value("size", 16);
			if (std::find(std::begin(kTextSizes), std::end(kTextSizes), watch.textSize) == std::end(kTextSizes))
				watch.textSize = 16;
			std::string error;
			const auto index = ParseAddress(watch.expression, error);
			if (!index)
			{
				Log::Write("Global Editor: skipped saved global \"{}\": {}", watch.expression, error);
				continue;
			}
			watch.index = *index;
			watch.id = g_shared.nextId++;
			g_shared.watches.push_back(std::move(watch));
		}
	}

	// --- window (render thread) -----------------------------------------

	const ImVec4 kRed = { 1.0f, 0.35f, 0.35f, 1.0f };
	const ImVec4 kGreen = { 0.45f, 0.9f, 0.45f, 1.0f };
	const ImVec4 kDim = { 0.6f, 0.6f, 0.6f, 1.0f };

	struct Ui
	{
		char address[160] = {};
		char label[64] = {};
		int type = 0;
		int textSize = 0; // index into kTextSizes
		std::unordered_map<int, std::array<char, 128>> edits;
	};
	Ui g_ui;

	void TypeCombo(const char* id, int* type, int* textSize)
	{
		const float em = ImGui::GetFontSize();
		ImGui::SetNextItemWidth(em * 8);
		ImGui::Combo(id, type, kTypeNames, IM_ARRAYSIZE(kTypeNames));
		if (static_cast<Type>(*type) == Type::TextLabel)
		{
			ImGui::SameLine();
			ImGui::SetNextItemWidth(em * 5);
			const char* sizes[] = { "16", "24", "32", "64" };
			ImGui::Combo(std::format("bytes{}", id).c_str(), textSize, sizes, IM_ARRAYSIZE(sizes));
		}
	}

	void Draw(bool* open)
	{
		std::vector<Watch> watches;
		std::unordered_map<int, std::string> values;
		std::string message;
		bool messageIsError = false;
		{
			std::lock_guard lock(g_shared.mutex);
			watches = g_shared.watches;
			values = g_shared.values;
			message = g_shared.message;
			messageIsError = g_shared.messageIsError;
		}

		const float em = ImGui::GetFontSize();
		ImGui::SetNextWindowSize(ImVec2(em * 60, em * 30), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Global Editor", open))
		{
			ImGui::End();
			return;
		}

		ImGui::SeparatorText("Add");
		ImGui::SetNextItemWidth(em * 22);
		ImGui::InputTextWithHint("Global", "Global_1425247.f_12[3 /*2*/]", g_ui.address, sizeof(g_ui.address));
		ImGui::SameLine();
		ImGui::TextColored(kDim, "(?)");
		if (ImGui::BeginItemTooltip())
		{
			ImGui::TextUnformatted("Paste it as the decompiled script writes it:\n"
				"  Global_N           the base index (decimal or 0x hex)\n"
				"  .f_N               adds N\n"
				"  [i /*size*/]       adds 1 + i * size (size 1 without the comment)\n"
				"  +N                 adds N (Rampage's Add)");
			ImGui::EndTooltip();
		}
		std::string error;
		const auto index = g_ui.address[0] ? ParseAddress(g_ui.address, error) : std::nullopt;
		if (g_ui.address[0])
		{
			if (index)
				ImGui::TextColored(kDim, "= Global_%d  (block %d, slot 0x%X)", *index, *index >> 18, *index & 0x3FFFF);
			else
				ImGui::TextColored(kRed, "%s", error.c_str());
		}
		TypeCombo("Type", &g_ui.type, &g_ui.textSize);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(em * 12);
		ImGui::InputTextWithHint("Label", "optional", g_ui.label, sizeof(g_ui.label));
		ImGui::SameLine();
		ImGui::BeginDisabled(!index);
		if (ImGui::Button("Watch"))
		{
			Watch watch;
			watch.label = g_ui.label;
			watch.expression = g_ui.address;
			watch.index = *index;
			watch.type = static_cast<Type>(g_ui.type);
			watch.textSize = kTextSizes[g_ui.textSize];
			{
				std::lock_guard lock(g_shared.mutex);
				watch.id = g_shared.nextId++;
				g_shared.watches.push_back(watch);
			}
			MainThread::Post(Save);
			g_ui.label[0] = 0;
		}
		ImGui::EndDisabled();

		if (!message.empty())
			ImGui::TextColored(messageIsError ? kRed : kGreen, "%s", message.c_str());

		ImGui::SeparatorText(std::format("Watched ({})", watches.size()).c_str());
		const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
			ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;
		if (ImGui::BeginTable("watches", 6, flags, ImVec2(0, ImGui::GetContentRegionAvail().y)))
		{
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Label");
			ImGui::TableSetupColumn("Global");
			ImGui::TableSetupColumn("Type");
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("New value");
			ImGui::TableSetupColumn("");
			ImGui::TableHeadersRow();
			for (const Watch& watch : watches)
			{
				ImGui::PushID(watch.id);
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(watch.label.c_str());
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(watch.expression.c_str());
				if (ImGui::BeginItemTooltip())
				{
					ImGui::Text("Global_%d", watch.index);
					ImGui::EndTooltip();
				}
				ImGui::TableNextColumn();
				if (watch.type == Type::TextLabel)
					ImGui::Text("TEXT_LABEL_%d", watch.textSize - 1);
				else
					ImGui::TextUnformatted(kTypeNames[static_cast<int>(watch.type)]);
				ImGui::TableNextColumn();
				auto value = values.find(watch.id);
				ImGui::TextUnformatted(value != values.end() ? value->second.c_str() : "...");
				ImGui::TableNextColumn();
				auto& edit = g_ui.edits[watch.id];
				if (watch.type != Type::CharPtr)
				{
					ImGui::SetNextItemWidth(em * 10);
					const bool enter = ImGui::InputText("##new", edit.data(), edit.size(), ImGuiInputTextFlags_EnterReturnsTrue);
					ImGui::SameLine();
					if (ImGui::SmallButton("Set") || enter)
					{
						const Watch copy = watch;
						const std::string input = edit.data();
						MainThread::Post([copy, input] {
							const std::string why = Write(copy, input);
							Report(why.empty() ? std::format("Set {} to {}.", copy.expression, Format(copy)) : why, !why.empty());
						});
					}
				}
				ImGui::TableNextColumn();
				if (ImGui::SmallButton("Copy"))
					ImGui::SetClipboardText(watch.expression.c_str());
				ImGui::SameLine();
				if (ImGui::SmallButton("Remove"))
				{
					const int id = watch.id;
					{
						std::lock_guard lock(g_shared.mutex);
						std::erase_if(g_shared.watches, [id](const Watch& w) { return w.id == id; });
					}
					g_ui.edits.erase(id);
					MainThread::Post(Save);
				}
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		ImGui::End();
	}
}

namespace GlobalEditor
{
	void Register()
	{
		g_tool.draw = Draw;
		Overlay::Register(g_tool);
		Load();
	}

	void SetOpen(bool open)
	{
		Overlay::SetOpen(g_tool, open);
	}

	void Tick()
	{
		if (!g_tool.open)
			return;
		std::vector<Watch> watches;
		{
			std::lock_guard lock(g_shared.mutex);
			watches = g_shared.watches;
		}
		std::unordered_map<int, std::string> values;
		for (const Watch& watch : watches)
			values[watch.id] = Format(watch);
		std::lock_guard lock(g_shared.mutex);
		g_shared.values = std::move(values);
	}

	void Suspend()
	{
		Overlay::SetOpen(g_tool, false);
	}
}

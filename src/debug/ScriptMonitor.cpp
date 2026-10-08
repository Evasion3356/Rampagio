#include "ScriptMonitor.h"
#include "ScriptData.h"
#include "ScriptHooks.h"
#include "..\overlay\Overlay.h"
#include "..\MainThread.h"
#include "..\GamePointers.h"
#include "..\ScriptBytecode.h"
#include "..\ScriptVM.h"
#include "..\script.h"

#include "imgui.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
	using ScriptBytecode::Function;
	using ScriptHooks::Value;

	// --- shared between the script thread and the window ----------------

	struct ThreadInfo
	{
		std::uint32_t id = 0;
		std::uint32_t hash = 0;
		std::string name;
		bool known = false; // name came from the table
		rage::eThreadState state{};
		bool paused = false; // paused from here
		std::uint32_t pc = 0, fp = 0, sp = 0, stackSize = 0;
		std::string exitMessage;
		bool hasProgram = false;
		std::uint32_t codeSize = 0, codePages = 0, natives = 0, statics = 0, globals = 0, args = 0, strings = 0, refCount = 0;
		ScriptVM::Timing timing;
		ULONGLONG aliveMs = 0;
	};

	using FunctionList = std::shared_ptr<const std::vector<Function>>;

	struct Shared
	{
		std::mutex mutex;
		bool pointersOk = false;
		std::vector<ThreadInfo> threads;
		std::vector<ScriptHooks::FunctionHook> functionHooks;
		std::vector<ScriptHooks::NativeHook> nativeHooks;
		std::unordered_map<std::uint32_t, FunctionList> functions; // by script hash
		std::unordered_set<std::uint32_t> functionsPending;
		int freeStacks = -1;
		std::string message;
		bool messageIsError = false;
	};
	Shared g_shared;
	std::atomic<int> g_stackSize = 1024; // the Start Script size, for the free stack count

	Overlay::Tool g_tool = { "Script Monitor", nullptr };

	void Report(std::string text, bool error)
	{
		std::lock_guard lock(g_shared.mutex);
		g_shared.message = std::move(text);
		g_shared.messageIsError = error;
	}

	std::string ScriptLabel(std::uint32_t hash)
	{
		if (hash == 0)
			return "All scripts";
		if (const char* name = ScriptData::FindScriptName(hash))
			return name;
		return std::format("0x{:08X}", hash);
	}

	const char* StateName(rage::eThreadState state)
	{
		switch (static_cast<int>(state))
		{
		case 0: return "Idle";
		case 1: return "Running";
		case 2: return "Killed";
		case 3: return "Paused";
		default: return "Unknown";
		}
	}

	// --- script thread --------------------------------------------------

	std::unordered_map<std::uint32_t, ULONGLONG> g_firstSeen;  // thread id -> tick
	std::unordered_map<std::uint32_t, rage::eThreadState> g_pausedFrom; // thread id -> state before pause
	ULONGLONG g_stacksRead = 0;

	void Snapshot()
	{
		const GamePointers::Pointers* p = GamePointers::Get();
		std::vector<ThreadInfo> threads;
		const ULONGLONG now = GetTickCount64();
		if (p)
		{
			std::unordered_map<std::uint32_t, rage::scrProgram*> programs;
			for (int i = 0; i < 160; i++)
			{
				if (rage::scrProgram* program = p->ScriptPrograms[i])
					programs[program->m_NameHash] = program;
			}
			std::unordered_set<std::uint32_t> live;
			for (rage::scrThread* thread : *p->ScriptThreads)
			{
				if (!thread || thread->m_Context.m_ThreadId == 0)
					continue;
				const rage::scrThreadContext& ctx = thread->m_Context;
				ThreadInfo info;
				info.id = ctx.m_ThreadId;
				info.hash = ctx.m_ScriptHash;
				const char* name = ScriptData::FindScriptName(info.hash);
				info.known = name != nullptr;
				info.name = name ? name : std::format("0x{:08X}", info.hash);
				info.state = ctx.m_State;
				info.paused = g_pausedFrom.contains(info.id);
				info.pc = ctx.m_ProgramCounter;
				info.fp = ctx.m_FramePointer;
				info.sp = ctx.m_StackPointer;
				info.stackSize = ctx.m_StackSize;
				if (ctx.m_State == rage::eThreadState::killed && thread->m_ExitMessage)
					info.exitMessage = thread->m_ExitMessage;
				if (auto it = programs.find(info.hash); it != programs.end())
				{
					const rage::scrProgram* program = it->second;
					info.hasProgram = true;
					info.codeSize = program->m_CodeSize;
					info.codePages = program->GetNumCodePages();
					info.natives = program->m_NativeCount;
					info.statics = program->m_LocalCount;
					info.globals = program->m_GlobalCount;
					info.args = program->m_ArgCount;
					info.strings = program->m_StringsCount;
					info.refCount = program->m_RefCount;
				}
				info.timing = ScriptVM::GetTiming(info.id);
				const ULONGLONG first = g_firstSeen.try_emplace(info.id, now).first->second;
				info.aliveMs = now - first;
				live.insert(info.id);
				threads.push_back(std::move(info));
			}
			std::erase_if(g_firstSeen, [&](const auto& item) { return !live.contains(item.first); });
			std::erase_if(g_pausedFrom, [&](const auto& item) { return !live.contains(item.first); });
		}

		int freeStacks = -1;
		const bool readStacks = now - g_stacksRead > 250;
		if (readStacks)
		{
			freeStacks = MISC::GET_NUMBER_OF_FREE_STACKS_OF_THIS_SIZE(g_stackSize);
			g_stacksRead = now;
		}

		auto functionHooks = ScriptHooks::ListFunctionHooks();
		auto nativeHooks = ScriptHooks::ListNativeHooks();
		std::lock_guard lock(g_shared.mutex);
		g_shared.pointersOk = p != nullptr;
		g_shared.threads = std::move(threads);
		g_shared.functionHooks = std::move(functionHooks);
		g_shared.nativeHooks = std::move(nativeHooks);
		if (readStacks)
			g_shared.freeStacks = freeStacks;
	}

	rage::scrThread* FindThreadById(std::uint32_t id)
	{
		const GamePointers::Pointers* p = GamePointers::Get();
		if (!p)
			return nullptr;
		for (rage::scrThread* thread : *p->ScriptThreads)
		{
			if (thread && thread->m_Context.m_ThreadId == id)
				return thread;
		}
		return nullptr;
	}

	void RequestFunctions(std::uint32_t hash)
	{
		{
			std::lock_guard lock(g_shared.mutex);
			if (g_shared.functions.contains(hash) || !g_shared.functionsPending.insert(hash).second)
				return;
		}
		MainThread::Post([hash] {
			const rage::scrProgram* program = GamePointers::FindScriptProgram(hash);
			auto list = std::make_shared<const std::vector<Function>>(ScriptBytecode::ListFunctions(program));
			std::lock_guard lock(g_shared.mutex);
			g_shared.functionsPending.erase(hash);
			if (program)
				g_shared.functions[hash] = std::move(list);
			else
			{
				g_shared.message = std::format("{} isn't loaded, so its functions can't be read.", ScriptLabel(hash));
				g_shared.messageIsError = true;
			}
		});
	}

	// Requests the script and starts it once loaded; posts itself again
	// each frame until then (up to 5 s).
	struct StartJob
	{
		std::uint32_t hash;
		int stackSize;
		ULONGLONG deadline;

		void operator()() const
		{
			const std::string name = ScriptLabel(hash);
			if (!SCRIPT::DOES_SCRIPT_WITH_NAME_HASH_EXIST(hash))
				return Report(std::format("{} doesn't exist.", name), true);
			if (MISC::GET_NUMBER_OF_FREE_STACKS_OF_THIS_SIZE(stackSize) == 0)
				return Report(std::format("No free stacks of size {}.", stackSize), true);
			if (!SCRIPT::HAS_SCRIPT_WITH_NAME_HASH_LOADED(hash))
			{
				SCRIPT::REQUEST_SCRIPT_WITH_NAME_HASH(hash);
				if (GetTickCount64() > deadline)
					return Report(std::format("{} didn't load within 5 seconds.", name), true);
				MainThread::Post(*this);
				return;
			}
			const int id = SCRIPT::START_NEW_SCRIPT_WITH_NAME_HASH(hash, stackSize);
			SCRIPT::SET_SCRIPT_WITH_NAME_HASH_AS_NO_LONGER_NEEDED(hash);
			Report(id ? std::format("Started {} (thread {}).", name, id) : std::format("{} didn't start.", name), id == 0);
		}
	};

	// Terminates every live thread of the script; how many.
	int TerminateAll(std::uint32_t hash)
	{
		const GamePointers::Pointers* p = GamePointers::Get();
		if (!p)
			return 0;
		std::vector<int> ids;
		for (rage::scrThread* thread : *p->ScriptThreads)
		{
			if (thread && thread->m_Context.m_ThreadId && thread->m_Context.m_ScriptHash == hash &&
				thread->m_Context.m_State != rage::eThreadState::killed)
				ids.push_back(static_cast<int>(thread->m_Context.m_ThreadId));
		}
		for (int id : ids)
			SCRIPT::TERMINATE_THREAD(id);
		return static_cast<int>(ids.size());
	}

	// Rampage's Restart: terminate every thread of the script, wait until
	// none runs, then start it again (StartJob), with no arguments.
	struct RestartJob
	{
		std::uint32_t hash;
		int stackSize;
		ULONGLONG deadline;
		bool terminated = false;

		void operator()()
		{
			if (!terminated)
			{
				TerminateAll(hash);
				terminated = true;
				MainThread::Post(*this);
				return;
			}
			if (SCRIPT::GET_NUMBER_OF_THREADS_RUNNING_THE_SCRIPT_WITH_THIS_HASH(hash) > 0)
			{
				if (GetTickCount64() > deadline)
					return Report(std::format("{} was still running after 5 seconds; not restarted.", ScriptLabel(hash)), true);
				MainThread::Post(*this);
				return;
			}
			MainThread::Post(StartJob{ hash, stackSize, GetTickCount64() + 5000 });
		}
	};

	// --- window (render thread) -----------------------------------------

	struct StackSize
	{
		const char* name;
		int size;
	};
	// HorseMenu's list (HorseMenu\src\game\rdr\data\StackSizes.hpp), by size.
	constexpr StackSize kStackSizes[] = {
		{ "MICRO", 128 }, { "NET_FETCH_HIDEOUT_LEADER", 200 }, { "STABLE_MOUNT", 400 }, { "MINI", 512 },
		{ "CAMP_DOG", 600 }, { "ABILITY_CARD_EVENTS", 800 }, { "DEFAULT", 1024 }, { "HUB_EVENTS", 1026 },
		{ "UPDATE", 1300 }, { "MATCHMAKING", 1301 }, { "PLAYER_MENU_SCRIPT", 1400 }, { "POSSE_VERSUS_RACE", 1600 },
		{ "NET_BACKGROUND", 1631 }, { "POSSE_FEUD", 1800 }, { "PAUSE_MENU_SCRIPT", 2000 }, { "SAVE_MENU_EVENTS", 2024 },
		{ "SATCHEL_EVENTS", 2025 }, { "MAP_EVENTS", 2026 }, { "SHOP_EVENTS", 2027 }, { "BACKGROUND_SCRIPT", 2047 },
		{ "ROLE_PROGRESSION_EVENTS", 2048 }, { "NET_SYSTEM_EXTENDED", 2050 }, { "NET_CUTSCENE", 2051 }, { "COUPONS_EVENTS", 2053 },
		{ "NET_BEAT", 2452 }, { "REWARDS_EVENTS", 2549 }, { "FME_PV_SMALL", 3000 }, { "FME_THM_SMALL", 3001 },
		{ "FME_STD_SMALL", 3002 }, { "CAMPWORKS", 3081 }, { "MP_MISSION_DOWNLOADER", 3088 }, { "NET_GUN_FOR_HIRE_ONLINE", 3090 },
		{ "NET_BEAT_MANAGER", 3500 }, { "NET_MOONSHINE_PROPERTY", 3982 }, { "SCRIPT_XML", 4592 }, { "PAUSE_MENU_EVENT_SCRIPT", 4700 },
		{ "CAMP", 5000 }, { "STRANGER_MISSION_NON_FETCH", 5001 }, { "DB_MEGA", 5400 }, { "FME_PV_MEDIUM", 5500 },
		{ "FME_THM_MEDIUM", 5501 }, { "FME_STD_MEDIUM", 5502 }, { "REGION", 5503 }, { "SHOWS", 5504 },
		{ "FISHING", 5505 }, { "ENDFLOW", 5506 }, { "MISSION_INTRO", 6000 }, { "MINIGAME_INTRO", 6001 },
		{ "NET_MAIN", 6002 }, { "SHOP", 6005 }, { "NET_GUN_FOR_HIRE_OFFLINE", 6010 }, { "MINIGAME", 6500 },
		{ "CAMP_ITEM", 6700 }, { "FME_PV_LARGE", 7000 }, { "FME_THM_LARGE", 7001 }, { "FME_STD_LARGE", 7002 },
		{ "MISSION_TUTORIAL", 7300 }, { "AUTOSTART", 7301 }, { "STRANGER_MISSION_FETCH", 10000 }, { "MP_MISSION_LOBBY", 10001 },
		{ "CHARACTER_REROLL", 10003 }, { "MP_UGC_TRANSITION", 14335 }, { "TRANSITION", 25500 }, { "MISSION_CREATOR", 40500 },
		{ "MISSION", 45000 }, { "INSTANCED_CONTENT", 75000 },
	};

	const ImVec4 kRed = { 1.0f, 0.35f, 0.35f, 1.0f };
	const ImVec4 kGreen = { 0.45f, 0.9f, 0.45f, 1.0f };
	const ImVec4 kAmber = { 1.0f, 0.75f, 0.3f, 1.0f };
	const ImVec4 kDim = { 0.6f, 0.6f, 0.6f, 1.0f };

	bool ContainsNoCase(std::string_view haystack, std::string_view needle)
	{
		if (needle.empty())
			return true;
		auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
			[](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
		return it != haystack.end();
	}

	// The window's own state; only the render thread touches it.
	struct Ui
	{
		// what Draw copied out of g_shared this frame
		bool pointersOk = false;
		std::vector<ThreadInfo> threads;
		std::vector<ScriptHooks::FunctionHook> functionHooks;
		std::vector<ScriptHooks::NativeHook> nativeHooks;
		int freeStacks = -1;
		std::string message;
		bool messageIsError = false;

		std::uint32_t selectedId = 0;
		std::uint32_t selectedHash = 0;
		char threadFilter[64] = {};
		bool showKilled = true;

		// Start Script
		int startScript = -1; // index into ScriptNames()
		int startStackSize = 1024;
		std::uint32_t startSizedFor = 0; // the script startStackSize was picked for

		// force cleanup
		int cleanupFlags = 0;
		std::uint32_t cleanupFlagsFor = 0; // thread id the flags were defaulted for
		char startFilter[64] = {};

		// function hooks
		char functionText[64] = {};
		char functionFilter[64] = {};
		int functionReturns = -1; // -1 any
		ScriptHooks::ReturnSpec functionSpec;
		char functionString[256] = {};

		// native hooks
		int nativeNamespace = -1; // -1 all
		char nativeSearch[128] = {};
		std::vector<int> nativeMatches;
		bool nativeMatchesDirty = true;
		int nativeSelected = -1;
		int nativeMode = 0;
		int nativeScope = 0; // 0 selected script, 1 all scripts
		ScriptHooks::ReturnSpec nativeSpec;
		char nativeString[256] = {};
	};
	Ui g_ui;

	const ThreadInfo* Selected()
	{
		for (const ThreadInfo& info : g_ui.threads)
		{
			if (info.id == g_ui.selectedId)
				return &info;
		}
		return nullptr;
	}

	void Row(const char* key, const std::string& value)
	{
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::TextColored(kDim, "%s", key);
		ImGui::TableNextColumn();
		ImGui::TextUnformatted(value.c_str());
	}

	std::string Duration(ULONGLONG ms)
	{
		const ULONGLONG s = ms / 1000;
		if (s < 60)
			return std::format("{}s", s);
		if (s < 3600)
			return std::format("{}m {:02}s", s / 60, s % 60);
		return std::format("{}h {:02}m {:02}s", s / 3600, s / 60 % 60, s % 60);
	}

	// The value inputs for a return kind.
	void ValueInputs(ScriptHooks::ReturnSpec& spec, char* stringBuffer, std::size_t stringSize)
	{
		switch (spec.kind)
		{
		case Value::Int:
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10);
			ImGui::InputInt("Value##int", &spec.i, 1, 100);
			ImGui::SameLine();
			ImGui::TextColored(kDim, "0x%08X", static_cast<std::uint32_t>(spec.i));
			break;
		case Value::Float:
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10);
			ImGui::InputFloat("Value##float", &spec.f, 0.0f, 0.0f, "%.4f");
			break;
		case Value::Vector3:
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 20);
			ImGui::InputFloat3("Value##vec", spec.v, "%.4f");
			break;
		case Value::String:
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 20);
			ImGui::InputText("Value##str", stringBuffer, stringSize);
			ImGui::TextColored(kAmber, "Returns a pointer to a copy of this text. A script that writes to it or keeps it may crash.");
			spec.s = stringBuffer;
			break;
		default:
			break;
		}
	}

	// The return kinds whose size matches `slots`.
	std::vector<Value> ChoicesForSlots(int slots)
	{
		switch (slots)
		{
		case 0: return { Value::Void };
		case 1: return { Value::True, Value::False, Value::Int, Value::Float, Value::String };
		case 3: return { Value::Vector3 };
		default: return {};
		}
	}

	std::vector<Value> ChoicesForNative(ScriptData::NativeReturn returns)
	{
		using ScriptData::NativeReturn;
		switch (returns)
		{
		case NativeReturn::Int: return { Value::True, Value::False, Value::Int };
		case NativeReturn::Float: return { Value::Float };
		case NativeReturn::Vector3: return { Value::Vector3 };
		case NativeReturn::String: return { Value::String };
		case NativeReturn::Pointer: return { Value::Int };
		default: return {};
		}
	}

	// A combo over `choices` editing spec.kind; keeps the kind valid.
	void ReturnCombo(const char* label, const std::vector<Value>& choices, ScriptHooks::ReturnSpec& spec)
	{
		if (choices.empty())
			return;
		if (std::find(choices.begin(), choices.end(), spec.kind) == choices.end())
			spec.kind = choices.front();
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
		if (ImGui::BeginCombo(label, ScriptHooks::ValueName(spec.kind)))
		{
			for (Value value : choices)
			{
				if (ImGui::Selectable(ScriptHooks::ValueName(value), value == spec.kind))
					spec.kind = value;
			}
			ImGui::EndCombo();
		}
	}

	// The flag bits the 1491.50 scripts check, with how many check each
	// (tools/extract_cleanup_flags.py's data).
	struct CleanupBit
	{
		int bit;
		int scripts;
	};
	constexpr CleanupBit kCleanupBits[] = { { 0, 1169 }, { 1, 1524 }, { 2, 8 }, { 3, 1337 }, { 5, 859 },
		{ 7, 9 }, { 9, 1513 }, { 11, 9 }, { 12, 231 } };
	constexpr int kAllCleanupFlags = 0x1AAF;

	void CleanupFlagsInput()
	{
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7);
		ImGui::InputScalar("Flags", ImGuiDataType_S32, &g_ui.cleanupFlags, nullptr, nullptr, "%X", ImGuiInputTextFlags_CharsHexadecimal);
		for (int i = 0; i < static_cast<int>(std::size(kCleanupBits)); i++)
		{
			const CleanupBit& bit = kCleanupBits[i];
			if (i % 3)
				ImGui::SameLine(ImGui::GetFontSize() * 9 * (i % 3));
			ImGui::CheckboxFlags(std::format("0x{:X} ({} scripts)", 1 << bit.bit, bit.scripts).c_str(), &g_ui.cleanupFlags, 1 << bit.bit);
		}
		if (ImGui::SmallButton("Every flag scripts check"))
			g_ui.cleanupFlags = kAllCleanupFlags;
		ImGui::SameLine();
		if (ImGui::SmallButton("Rampage's (0x800)"))
			g_ui.cleanupFlags = 0x800;
	}

	void DrawThreadList()
	{
		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::InputTextWithHint("##filter", "Filter by name or hash", g_ui.threadFilter, sizeof(g_ui.threadFilter));
		ImGui::Checkbox("Show killed", &g_ui.showKilled);
		ImGui::SameLine();
		ImGui::TextColored(kDim, "%zu threads", g_ui.threads.size());

		const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
			ImGuiTableFlags_Sortable | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;
		const float height = ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 6.5f;
		if (!ImGui::BeginTable("threads", 4, flags, ImVec2(0, (std::max)(height, ImGui::GetFontSize() * 8))))
			return;
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Script", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort);
		ImGui::TableSetupColumn("ID");
		ImGui::TableSetupColumn("State");
		ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_PreferSortDescending);
		ImGui::TableHeadersRow();

		std::vector<const ThreadInfo*> rows;
		for (const ThreadInfo& info : g_ui.threads)
		{
			if (!g_ui.showKilled && info.state == rage::eThreadState::killed)
				continue;
			if (!ContainsNoCase(info.name, g_ui.threadFilter) && !ContainsNoCase(std::format("{:08X}", info.hash), g_ui.threadFilter))
				continue;
			rows.push_back(&info);
		}
		if (ImGuiTableSortSpecs* sort = ImGui::TableGetSortSpecs(); sort && sort->SpecsCount > 0)
		{
			const ImGuiTableColumnSortSpecs& spec = sort->Specs[0];
			const bool ascending = spec.SortDirection == ImGuiSortDirection_Ascending;
			std::stable_sort(rows.begin(), rows.end(), [&](const ThreadInfo* a, const ThreadInfo* b) {
				int order = 0;
				switch (spec.ColumnIndex)
				{
				case 0: order = a->name.compare(b->name); break;
				case 1: order = a->id < b->id ? -1 : a->id > b->id; break;
				case 2: order = static_cast<int>(a->state) - static_cast<int>(b->state); break;
				case 3: order = a->timing.averageMs < b->timing.averageMs ? -1 : a->timing.averageMs > b->timing.averageMs; break;
				}
				return ascending ? order < 0 : order > 0;
			});
		}

		ImGuiListClipper clipper;
		clipper.Begin(static_cast<int>(rows.size()));
		while (clipper.Step())
		{
			for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
			{
				const ThreadInfo& info = *rows[i];
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				const bool killed = info.state == rage::eThreadState::killed;
				if (killed)
					ImGui::PushStyleColor(ImGuiCol_Text, kRed);
				else if (!info.known)
					ImGui::PushStyleColor(ImGuiCol_Text, kAmber);
				ImGui::PushID(static_cast<int>(info.id));
				if (ImGui::Selectable(info.name.c_str(), info.id == g_ui.selectedId, ImGuiSelectableFlags_SpanAllColumns))
				{
					g_ui.selectedId = info.id;
					g_ui.selectedHash = info.hash;
				}
				ImGui::PopID();
				if (killed || !info.known)
					ImGui::PopStyleColor();
				ImGui::TableNextColumn();
				ImGui::Text("%u", info.id);
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(info.paused ? "Paused" : StateName(info.state));
				ImGui::TableNextColumn();
				ImGui::Text("%.3f", info.timing.averageMs);
			}
		}
		ImGui::EndTable();
	}

	void DrawStartScript()
	{
		if (!ImGui::CollapsingHeader("Start Script"))
			return;
		const auto names = ScriptData::ScriptNames();
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::BeginCombo("##script", g_ui.startScript >= 0 ? names[g_ui.startScript].name : "(select a script)", ImGuiComboFlags_HeightLarge))
		{
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::IsWindowAppearing())
				ImGui::SetKeyboardFocusHere();
			ImGui::InputTextWithHint("##startfilter", "Search", g_ui.startFilter, sizeof(g_ui.startFilter));
			std::vector<int> matches;
			for (int i = 0; i < static_cast<int>(names.size()); i++)
			{
				if (ContainsNoCase(names[i].name, g_ui.startFilter))
					matches.push_back(i);
			}
			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(matches.size()));
			while (clipper.Step())
			{
				for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++)
				{
					const int i = matches[row];
					if (ImGui::Selectable(names[i].name, i == g_ui.startScript))
						g_ui.startScript = i;
				}
			}
			ImGui::EndCombo();
		}

		// A newly picked script gets the stack size it's normally started with.
		const std::uint32_t hash = g_ui.startScript >= 0 ? names[g_ui.startScript].hash : 0;
		const int knownSize = hash ? ScriptData::StackSize(hash) : 0;
		if (hash != g_ui.startSizedFor)
		{
			g_ui.startSizedFor = hash;
			if (knownSize > 0)
				g_ui.startStackSize = knownSize;
		}

		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7);
		ImGui::InputInt("##stacksize", &g_ui.startStackSize, 0, 0);
		g_ui.startStackSize = std::clamp(g_ui.startStackSize, 1, 200000);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-FLT_MIN);
		const StackSize* named = nullptr;
		for (const StackSize& stack : kStackSizes)
		{
			if (stack.size == g_ui.startStackSize)
				named = &stack;
		}
		if (ImGui::BeginCombo("##stack", named ? named->name : "(custom)", ImGuiComboFlags_HeightLarge))
		{
			for (const StackSize& stack : kStackSizes)
			{
				if (ImGui::Selectable(std::format("{} ({})", stack.name, stack.size).c_str(), &stack == named))
					g_ui.startStackSize = stack.size;
			}
			ImGui::EndCombo();
		}
		g_stackSize = g_ui.startStackSize;
		if (hash)
		{
			int running = 0;
			for (const ThreadInfo& info : g_ui.threads)
				running += info.hash == hash && info.state != rage::eThreadState::killed;
			if (knownSize > 0)
				ImGui::TextColored(kDim, "Usual stack size: %d.  Running: %d.", knownSize, running);
			else
				ImGui::TextColored(kDim, "Usual stack size unknown.  Running: %d.", running);
		}

		ImGui::BeginDisabled(g_ui.startScript < 0);
		if (ImGui::Button("Start"))
			MainThread::Post(StartJob{ hash, g_ui.startStackSize, GetTickCount64() + 5000 });
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::TextColored(kDim, "Free stacks of this size: %d", g_ui.freeStacks);

		ImGui::Spacing();
		if (ImGui::Button("Force Cleanup All Scripts..."))
			ImGui::OpenPopup("cleanupall");
		if (ImGui::BeginPopup("cleanupall"))
		{
			ImGui::TextUnformatted("FORCE_CLEANUP: every script that registered for these\ncleanup flags cleans up (usually missions and activities).");
			CleanupFlagsInput();
			if (ImGui::Button("Force Cleanup All"))
			{
				const int flags = g_ui.cleanupFlags;
				MainThread::Post([flags] {
					PLAYER::FORCE_CLEANUP(flags);
					Report(std::format("FORCE_CLEANUP(0x{:X}).", flags), false);
				});
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
	}

	void DrawThreadTab(const ThreadInfo& info)
	{
		if (ImGui::BeginTable("details", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg))
		{
			Row("Name", info.name + (info.known ? "" : "  (not a 1491.50 singleplayer script name)"));
			Row("Hash", std::format("0x{:08X}  ({})", info.hash, static_cast<std::int32_t>(info.hash)));
			Row("Thread ID", std::to_string(info.id));
			Row("State", info.paused ? "Paused (from here)" : StateName(info.state));
			Row("Program counter", std::format("0x{:X}", info.pc));
			Row("Frame pointer", std::to_string(info.fp));
			Row("Stack pointer", std::to_string(info.sp));
			Row("Stack size", std::format("{} slots", info.stackSize));
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextColored(kDim, "Stack used");
			ImGui::TableNextColumn();
			const float used = info.stackSize ? static_cast<float>(info.sp) / static_cast<float>(info.stackSize) : 0.0f;
			ImGui::ProgressBar(used, ImVec2(ImGui::GetFontSize() * 14, 0), std::format("{:.1f}%", used * 100).c_str());
			Row("VM time", std::format("{:.3f} ms last frame, {:.3f} ms average, {:.3f} ms peak", info.timing.lastFrameMs,
				info.timing.averageMs, info.timing.peakMs));
			Row("VM calls", std::format("{} last frame", info.timing.callsLastFrame));
			Row("Running for", Duration(info.aliveMs) + "  (since the monitor first saw it)");
			if (info.hasProgram)
			{
				Row("Code size", std::format("{} bytes ({} pages)", info.codeSize, info.codePages));
				Row("Natives", std::to_string(info.natives));
				Row("Statics", std::to_string(info.statics));
				Row("Globals block", std::to_string(info.globals));
				Row("Arguments", std::to_string(info.args));
				Row("String heap", std::format("{} bytes", info.strings));
				Row("Program refs", std::to_string(info.refCount));
			}
			else
				Row("Program", "not loaded");
			if (!info.exitMessage.empty())
				Row("Exit reason", info.exitMessage);
			ImGui::EndTable();
		}

		const std::uint32_t id = info.id;
		const std::uint32_t hash = info.hash;
		if (g_ui.cleanupFlagsFor != id)
		{
			// Default to the flags this script checks, so Force Cleanup does something.
			g_ui.cleanupFlagsFor = id;
			const std::uint32_t flags = ScriptData::CleanupFlags(hash);
			g_ui.cleanupFlags = static_cast<int>(flags ? flags : kAllCleanupFlags);
		}
		int instances = 0;
		for (const ThreadInfo& other : g_ui.threads)
			instances += other.hash == hash && other.state != rage::eThreadState::killed;

		if (info.state != rage::eThreadState::killed)
		{
			if (ImGui::Button("Kill"))
			{
				MainThread::Post([id] {
					SCRIPT::TERMINATE_THREAD(static_cast<int>(id));
					Report(std::format("Terminated thread {}.", id), false);
				});
			}
			ImGui::SameLine();
			if (instances > 1)
			{
				if (ImGui::Button(std::format("Kill all {}", instances).c_str()))
				{
					MainThread::Post([hash] {
						const int killed = TerminateAll(hash);
						Report(std::format("Terminated {} thread(s) of {}.", killed, ScriptLabel(hash)), false);
					});
				}
				ImGui::SameLine();
			}
			if (ImGui::Button(info.paused ? "Resume" : "Pause"))
			{
				MainThread::Post([id] {
					rage::scrThread* thread = FindThreadById(id);
					if (!thread)
						return;
					if (auto it = g_pausedFrom.find(id); it != g_pausedFrom.end())
					{
						thread->m_Context.m_State = it->second;
						g_pausedFrom.erase(it);
					}
					else
					{
						g_pausedFrom[id] = thread->m_Context.m_State;
						thread->m_Context.m_State = static_cast<rage::eThreadState>(3);
					}
				});
			}
			ImGui::SameLine();
		}
		// Restart works on a killed thread too: it starts the script again.
		const int restartSize = info.stackSize ? static_cast<int>(info.stackSize) : ScriptData::StackSize(hash);
		ImGui::BeginDisabled(restartSize <= 0);
		if (ImGui::Button("Restart"))
			MainThread::Post(RestartJob{ hash, restartSize, GetTickCount64() + 5000 });
		ImGui::EndDisabled();
		if (ImGui::BeginItemTooltip())
		{
			ImGui::Text("Terminates every thread of %s, waits until none runs,\nthen starts it again with stack size %d and no arguments.",
				info.name.c_str(), restartSize);
			ImGui::EndTooltip();
		}
		if (info.state != rage::eThreadState::killed)
		{
			ImGui::SameLine();
			if (ImGui::Button("Force Cleanup..."))
				ImGui::OpenPopup("cleanup");
			if (ImGui::BeginPopup("cleanup"))
			{
				ImGui::Text("FORCE_CLEANUP_FOR_THREAD_WITH_THIS_ID(%u, flags):\nthe thread runs its cleanup if it registered for these flags.", id);
				CleanupFlagsInput();
				if (ImGui::Button("Force Cleanup"))
				{
					const int flags = g_ui.cleanupFlags;
					MainThread::Post([id, flags] {
						PLAYER::FORCE_CLEANUP_FOR_THREAD_WITH_THIS_ID(static_cast<int>(id), flags);
						Report(std::format("FORCE_CLEANUP_FOR_THREAD_WITH_THIS_ID({}, 0x{:X}).", id, flags), false);
					});
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (ImGui::Button("Cancel"))
					ImGui::CloseCurrentPopup();
				ImGui::EndPopup();
			}
		}
		if (ImGui::Button("Copy name"))
			ImGui::SetClipboardText(info.name.c_str());
		ImGui::SameLine();
		if (ImGui::Button("Copy hash"))
			ImGui::SetClipboardText(std::format("0x{:08X}", info.hash).c_str());
	}

	void DrawFunctionsTab(const ThreadInfo& info)
	{
		FunctionList functions;
		bool pending = false;
		{
			std::lock_guard lock(g_shared.mutex);
			if (auto it = g_shared.functions.find(info.hash); it != g_shared.functions.end())
				functions = it->second;
			pending = g_shared.functionsPending.contains(info.hash);
		}
		if (!functions)
		{
			if (!pending)
				RequestFunctions(info.hash);
			ImGui::TextColored(kDim, "Reading the bytecode...");
			return;
		}

		// --- add a hook
		ImGui::SeparatorText("Add Hook");
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
		ImGui::InputTextWithHint("Function", "func_688 or 0x3A7", g_ui.functionText, sizeof(g_ui.functionText));
		ImGui::SameLine();
		ImGui::TextColored(kDim, "(?)");
		if (ImGui::BeginItemTooltip())
		{
			ImGui::TextUnformatted("As the decompiled script names it: func_N, __EntryFunction__,\nor the \"// Position - 0x...\" from its header line.");
			ImGui::EndTooltip();
		}

		const Function* function = nullptr;
		if (g_ui.functionText[0])
		{
			const auto ref = ScriptBytecode::ParseFunctionRef(g_ui.functionText);
			function = ref ? ScriptBytecode::Find(*functions, *ref) : nullptr;
			if (!ref)
				ImGui::TextColored(kRed, "Not a function name or position.");
			else if (!function)
				ImGui::TextColored(kRed, "%s has no such function (%zu functions).", info.name.c_str(), functions->size());
		}
		if (function)
		{
			ImGui::Text("func_%u: %u arg(s), returns %u slot(s), Position 0x%X, ENTER at 0x%X, %u bytes", function->index,
				function->argCount, function->returnCount, function->start, function->enter, function->end - function->start);
			const auto choices = ChoicesForSlots(function->returnCount);
			if (choices.empty())
				ImGui::TextColored(kAmber, "Returns %u slots (a struct); only 0, 1 or 3 can be hooked.", function->returnCount);
			else
			{
				ReturnCombo("Return", choices, g_ui.functionSpec);
				ValueInputs(g_ui.functionSpec, g_ui.functionString, sizeof(g_ui.functionString));
				if (ImGui::Button("Add Hook"))
				{
					const std::uint32_t hash = info.hash;
					const Function copy = *function;
					const ScriptHooks::ReturnSpec spec = g_ui.functionSpec;
					MainThread::Post([hash, copy, spec] {
						const std::string error = ScriptHooks::AddFunctionHook(hash, copy, spec);
						Report(error.empty() ? std::format("Hooked func_{}: {}.", copy.index, ScriptHooks::Describe(spec)) : error, !error.empty());
					});
				}
			}
		}

		// --- this script's hooks
		bool any = false;
		for (const auto& hook : g_ui.functionHooks)
			any = any || hook.script == info.hash;
		if (any)
		{
			ImGui::SeparatorText("Hooks in this script");
			for (const auto& hook : g_ui.functionHooks)
			{
				if (hook.script != info.hash)
					continue;
				ImGui::PushID(hook.id);
				const int id = hook.id;
				if (ImGui::SmallButton("Remove"))
					MainThread::Post([id] { ScriptHooks::RemoveFunctionHook(id); });
				ImGui::SameLine();
				ImGui::Text("func_%u -> %s", hook.index, ScriptHooks::Describe(hook.returns).c_str());
				ImGui::PopID();
			}
		}

		// --- every function
		ImGui::SeparatorText(std::format("Functions ({})", functions->size()).c_str());
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10);
		ImGui::InputTextWithHint("##ffilter", "func_ filter", g_ui.functionFilter, sizeof(g_ui.functionFilter));
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
		const char* returnsLabels[] = { "Any return", "Returns 0", "Returns 1", "Returns 3" };
		int returnsIndex = g_ui.functionReturns < 0 ? 0 : (g_ui.functionReturns == 3 ? 3 : g_ui.functionReturns + 1);
		if (ImGui::Combo("##freturns", &returnsIndex, returnsLabels, IM_ARRAYSIZE(returnsLabels)))
			g_ui.functionReturns = returnsIndex == 0 ? -1 : (returnsIndex == 3 ? 3 : returnsIndex - 1);

		std::vector<const Function*> rows;
		for (const Function& f : *functions)
		{
			if (g_ui.functionReturns >= 0 && f.returnCount != g_ui.functionReturns)
				continue;
			if (g_ui.functionFilter[0] && !ContainsNoCase(std::format("func_{}", f.index), g_ui.functionFilter))
				continue;
			rows.push_back(&f);
		}
		const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
		if (ImGui::BeginTable("functions", 6, flags, ImVec2(0, ImGui::GetContentRegionAvail().y)))
		{
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Function", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Position");
			ImGui::TableSetupColumn("ENTER");
			ImGui::TableSetupColumn("Args");
			ImGui::TableSetupColumn("Returns");
			ImGui::TableSetupColumn("Bytes");
			ImGui::TableHeadersRow();
			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(rows.size()));
			while (clipper.Step())
			{
				for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
				{
					const Function& f = *rows[i];
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					const std::string label = f.index == 0 ? "__EntryFunction__" : std::format("func_{}", f.index);
					if (ImGui::Selectable(label.c_str(), function == &f, ImGuiSelectableFlags_SpanAllColumns))
						std::snprintf(g_ui.functionText, sizeof(g_ui.functionText), "func_%u", f.index);
					ImGui::TableNextColumn();
					ImGui::Text("0x%X", f.start);
					ImGui::TableNextColumn();
					ImGui::Text("0x%X", f.enter);
					ImGui::TableNextColumn();
					ImGui::Text("%u", f.argCount);
					ImGui::TableNextColumn();
					ImGui::Text("%u", f.returnCount);
					ImGui::TableNextColumn();
					ImGui::Text("%u", f.end - f.start);
				}
			}
			ImGui::EndTable();
		}
	}

	void RefreshNativeMatches()
	{
		g_ui.nativeMatches.clear();
		const auto natives = ScriptData::Natives();
		const auto namespaces = ScriptData::NativeNamespaces();
		const char* nameSpace = g_ui.nativeNamespace >= 0 ? namespaces[g_ui.nativeNamespace] : nullptr;
		std::string_view search = g_ui.nativeSearch;
		const bool byHash = search.starts_with("0x") || search.starts_with("0X");
		if (byHash)
			search.remove_prefix(2);
		for (int i = 0; i < static_cast<int>(natives.size()); i++)
		{
			const ScriptData::Native& native = natives[i];
			if (nameSpace && std::strcmp(native.nameSpace, nameSpace) != 0)
				continue;
			const bool match = byHash ? ContainsNoCase(std::format("{:016X}", native.hash), search)
				: ContainsNoCase(native.name, search) || ContainsNoCase(std::format("{:016X}", native.hash), search);
			if (match)
				g_ui.nativeMatches.push_back(i);
		}
		g_ui.nativeMatchesDirty = false;
	}

	void DrawNativesTab(const ThreadInfo* selected)
	{
		const auto natives = ScriptData::Natives();
		const auto namespaces = ScriptData::NativeNamespaces();

		ImGui::SeparatorText("Add Hook");
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
		if (ImGui::BeginCombo("Namespace", g_ui.nativeNamespace >= 0 ? namespaces[g_ui.nativeNamespace] : "(all)", ImGuiComboFlags_HeightLarge))
		{
			if (ImGui::Selectable("(all)", g_ui.nativeNamespace < 0))
			{
				g_ui.nativeNamespace = -1;
				g_ui.nativeMatchesDirty = true;
			}
			for (int i = 0; i < static_cast<int>(namespaces.size()); i++)
			{
				if (ImGui::Selectable(namespaces[i], i == g_ui.nativeNamespace))
				{
					g_ui.nativeNamespace = i;
					g_ui.nativeMatchesDirty = true;
				}
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16);
		if (ImGui::InputTextWithHint("##nsearch", "Name or hash (0x...)", g_ui.nativeSearch, sizeof(g_ui.nativeSearch)))
			g_ui.nativeMatchesDirty = true;
		if (g_ui.nativeMatchesDirty)
			RefreshNativeMatches();
		ImGui::SameLine();
		ImGui::TextColored(kDim, "%zu natives", g_ui.nativeMatches.size());

		if (ImGui::BeginChild("natives", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 9), ImGuiChildFlags_Borders))
		{
			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(g_ui.nativeMatches.size()));
			while (clipper.Step())
			{
				for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++)
				{
					const int i = g_ui.nativeMatches[row];
					const ScriptData::Native& native = natives[i];
					ImGui::PushID(i);
					if (ImGui::Selectable(native.name, i == g_ui.nativeSelected))
						g_ui.nativeSelected = i;
					ImGui::PopID();
					ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.6f);
					ImGui::TextColored(kDim, "%s  0x%016llX", native.nameSpace, static_cast<unsigned long long>(native.hash));
				}
			}
		}
		ImGui::EndChild();

		if (g_ui.nativeSelected >= 0)
		{
			const ScriptData::Native& native = natives[g_ui.nativeSelected];
			ImGui::Text("%s %s::%s(%s)", native.returnType, native.nameSpace, native.name, native.parameters);
			ImGui::TextColored(kDim, "0x%016llX", static_cast<unsigned long long>(native.hash));

			const bool isVoid = native.returns == ScriptData::NativeReturn::Void;
			const ScriptHooks::NativeMode modes[] = { ScriptHooks::NativeMode::Return, ScriptHooks::NativeMode::CallThenReturn,
				ScriptHooks::NativeMode::LogOnly };
			if (isVoid && g_ui.nativeMode == 1)
				g_ui.nativeMode = 0;
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
			if (ImGui::BeginCombo("Mode", isVoid && g_ui.nativeMode == 0 ? "Skip native" : ScriptHooks::NativeModeName(modes[g_ui.nativeMode])))
			{
				for (int i = 0; i < 3; i++)
				{
					if (isVoid && i == 1)
						continue;
					const char* label = isVoid && i == 0 ? "Skip native" : ScriptHooks::NativeModeName(modes[i]);
					if (ImGui::Selectable(label, i == g_ui.nativeMode))
						g_ui.nativeMode = i;
				}
				ImGui::EndCombo();
			}
			const auto mode = modes[g_ui.nativeMode];
			if (isVoid)
				g_ui.nativeSpec.kind = Value::Void;
			else if (mode != ScriptHooks::NativeMode::LogOnly)
			{
				ReturnCombo("Return", ChoicesForNative(native.returns), g_ui.nativeSpec);
				ValueInputs(g_ui.nativeSpec, g_ui.nativeString, sizeof(g_ui.nativeString));
				if (native.returns == ScriptData::NativeReturn::Pointer)
					ImGui::TextColored(kAmber, "Returns a pointer; anything but 0 will likely crash the script.");
			}

			if (selected)
				ImGui::RadioButton(std::format("Only {}", selected->name).c_str(), &g_ui.nativeScope, 0);
			else
			{
				g_ui.nativeScope = 1;
				ImGui::BeginDisabled();
				ImGui::RadioButton("Only the selected script", &g_ui.nativeScope, 0);
				ImGui::EndDisabled();
			}
			ImGui::SameLine();
			ImGui::RadioButton("All scripts", &g_ui.nativeScope, 1);

			if (ImGui::Button("Add Hook"))
			{
				const ScriptData::Native* chosen = &native;
				const std::uint32_t script = g_ui.nativeScope == 0 && selected ? selected->hash : 0;
				ScriptHooks::ReturnSpec spec = g_ui.nativeSpec;
				if (mode == ScriptHooks::NativeMode::LogOnly)
					spec.kind = Value::Void;
				MainThread::Post([chosen, script, mode, spec] {
					const std::string error = ScriptHooks::AddNativeHook(script, *chosen, mode, spec);
					Report(error.empty() ? std::format("Hooked {} in {}.", chosen->name, ScriptLabel(script)) : error, !error.empty());
				});
			}
		}

		ImGui::SeparatorText(std::format("Native Hooks ({})", g_ui.nativeHooks.size()).c_str());
		const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
			ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Resizable;
		if (ImGui::BeginTable("nativehooks", 7, flags, ImVec2(0, ImGui::GetContentRegionAvail().y)))
		{
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Native", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Scope");
			ImGui::TableSetupColumn("Mode");
			ImGui::TableSetupColumn("Returns");
			ImGui::TableSetupColumn("Calls");
			ImGui::TableSetupColumn("Last caller");
			ImGui::TableSetupColumn("");
			ImGui::TableHeadersRow();
			for (const auto& hook : g_ui.nativeHooks)
			{
				ImGui::PushID(hook.id);
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(hook.native->name);
				if (hook.calls && ImGui::BeginItemTooltip())
				{
					ImGui::Text("%s(%s)", hook.native->name, hook.native->parameters);
					const int logged = (std::min)(static_cast<int>(hook.native->parameterCount), ScriptHooks::kLoggedArgs);
					for (int i = 0; i < logged; i++)
					{
						float asFloat;
						std::memcpy(&asFloat, &hook.lastArgs[i], sizeof(float));
						ImGui::Text("arg %d: 0x%llX  int %d  float %g", i, static_cast<unsigned long long>(hook.lastArgs[i]),
							static_cast<std::int32_t>(hook.lastArgs[i]), asFloat);
					}
					ImGui::EndTooltip();
				}
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(ScriptLabel(hook.script).c_str());
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(ScriptHooks::NativeModeName(hook.mode));
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(hook.mode == ScriptHooks::NativeMode::LogOnly ? "-" : ScriptHooks::Describe(hook.returns).c_str());
				ImGui::TableNextColumn();
				ImGui::Text("%llu", static_cast<unsigned long long>(hook.calls));
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(hook.calls ? ScriptLabel(hook.lastCaller).c_str() : "-");
				ImGui::TableNextColumn();
				const int id = hook.id;
				if (ImGui::SmallButton("Reset"))
					MainThread::Post([id] { ScriptHooks::ResetNativeHookStats(id); });
				ImGui::SameLine();
				if (ImGui::SmallButton("Remove"))
					MainThread::Post([id] { ScriptHooks::RemoveNativeHook(id); });
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
	}

	void DrawAllHooksTab()
	{
		ImGui::SeparatorText(std::format("Function Hooks ({})", g_ui.functionHooks.size()).c_str());
		for (const auto& hook : g_ui.functionHooks)
		{
			ImGui::PushID(hook.id);
			const int id = hook.id;
			if (ImGui::SmallButton("Remove"))
				MainThread::Post([id] { ScriptHooks::RemoveFunctionHook(id); });
			ImGui::SameLine();
			ImGui::Text("%s  func_%u(%u args) -> %s", ScriptLabel(hook.script).c_str(), hook.index, hook.argCount,
				ScriptHooks::Describe(hook.returns).c_str());
			ImGui::PopID();
		}
		ImGui::SeparatorText(std::format("Native Hooks ({})", g_ui.nativeHooks.size()).c_str());
		for (const auto& hook : g_ui.nativeHooks)
		{
			ImGui::PushID(hook.id + 0x100000);
			const int id = hook.id;
			if (ImGui::SmallButton("Remove"))
				MainThread::Post([id] { ScriptHooks::RemoveNativeHook(id); });
			ImGui::SameLine();
			ImGui::Text("%s  %s::%s  %s  (%llu calls)", ScriptLabel(hook.script).c_str(), hook.native->nameSpace, hook.native->name,
				ScriptHooks::NativeModeName(hook.mode), static_cast<unsigned long long>(hook.calls));
			ImGui::PopID();
		}
		if (!g_ui.functionHooks.empty() || !g_ui.nativeHooks.empty())
		{
			ImGui::Spacing();
			if (ImGui::Button("Remove all hooks"))
				MainThread::Post([] { ScriptHooks::RemoveAll(); });
		}
	}

	void Draw(bool* open)
	{
		{
			std::lock_guard lock(g_shared.mutex);
			g_ui.pointersOk = g_shared.pointersOk;
			g_ui.threads = g_shared.threads;
			g_ui.functionHooks = g_shared.functionHooks;
			g_ui.nativeHooks = g_shared.nativeHooks;
			g_ui.freeStacks = g_shared.freeStacks;
			g_ui.message = g_shared.message;
			g_ui.messageIsError = g_shared.messageIsError;
		}

		const float em = ImGui::GetFontSize();
		ImGui::SetNextWindowSize(ImVec2(em * 80, em * 48), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Script Monitor", open))
		{
			ImGui::End();
			return;
		}
		if (!g_ui.pointersOk)
			ImGui::TextColored(kRed, "The script thread pointers didn't resolve (see Rampagio.log).");
		ImGui::TextColored(kDim, "Renderer: %s.  F5 or the X closes this and gives input back to the game.", Overlay::ActiveBackendName());
		if (!g_ui.message.empty())
		{
			ImGui::SameLine();
			ImGui::TextColored(g_ui.messageIsError ? kRed : kGreen, "  %s", g_ui.message.c_str());
		}

		if (ImGui::BeginChild("left", ImVec2(em * 22, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX))
		{
			DrawThreadList();
			DrawStartScript();
		}
		ImGui::EndChild();
		ImGui::SameLine();

		if (ImGui::BeginChild("right", ImVec2(0, 0), ImGuiChildFlags_Borders))
		{
			const ThreadInfo* selected = Selected();
			if (ImGui::BeginTabBar("tabs"))
			{
				if (ImGui::BeginTabItem("Thread"))
				{
					if (selected)
						DrawThreadTab(*selected);
					else
						ImGui::TextColored(kDim, g_ui.selectedId ? "That thread is gone." : "Select a script on the left.");
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Functions"))
				{
					if (selected)
						DrawFunctionsTab(*selected);
					else
						ImGui::TextColored(kDim, "Select a script on the left.");
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Natives"))
				{
					DrawNativesTab(selected);
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem(std::format("All Hooks ({})###allhooks", g_ui.functionHooks.size() + g_ui.nativeHooks.size()).c_str()))
				{
					DrawAllHooksTab();
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}
		}
		ImGui::EndChild();
		ImGui::End();
	}
}

namespace ScriptMonitor
{
	void Register()
	{
		g_tool.draw = Draw;
		Overlay::Register(g_tool);
	}

	void SetOpen(bool open)
	{
		if (open)
			ScriptVM::EnableTiming();
		Overlay::SetOpen(g_tool, open);
	}

	bool IsOpen()
	{
		return g_tool.open;
	}

	void Tick()
	{
		ScriptVM::EndFrame();
		if (g_tool.open)
			Snapshot();
	}

	void Suspend()
	{
		Overlay::SetOpen(g_tool, false);
		for (const auto& [id, state] : g_pausedFrom)
		{
			if (rage::scrThread* thread = FindThreadById(id))
				thread->m_Context.m_State = state;
		}
		g_pausedFrom.clear();
		ScriptHooks::RemoveAll();
	}
}

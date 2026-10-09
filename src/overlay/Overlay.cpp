#include "OverlayInternal.h"
#include "..\Log.h"

#include "MinHook.h"

#include "imgui.h"
#include "backends/imgui_impl_win32.h"

#include <algorithm>
#include <array>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace Overlay
{
	std::atomic<bool> g_shutdown = false;
	std::atomic<bool> g_vulkan_present_seen = false;
	std::atomic<int> InHook::count = 0;

	namespace
	{
		constexpr ULONGLONG kDx12ClaimDelayMs = 3000;
		constexpr int kMaxTools = 8;

		std::atomic<Backend> g_backend = Backend::None;
		std::recursive_mutex g_imgui_mutex;
		HWND g_hwnd = nullptr;
		WNDPROC g_original_wndproc = nullptr;
		std::atomic<ULONGLONG> g_dx12_hooks_tick = 0;
		std::atomic<ULONGLONG> g_vulkan_hooks_tick = 0;
		std::atomic<int> g_close_key = VK_F5;
		std::atomic<int> g_swallow_key_up = 0;

		// Written on the script thread before the overlay starts, read on the render thread.
		std::array<Tool*, kMaxTools> g_tools = {};
		std::atomic<int> g_tool_count = 0;
		std::atomic<bool> g_any_open = false;    // an interactive tool: input goes to ImGui
		std::atomic<bool> g_any_visible = false; // any tool, passive ones too: something to draw

		bool g_started = false; // script thread
		std::atomic<bool> g_init_running = false;

		std::mutex g_hooks_mutex;
		std::vector<void*> g_hook_targets;

		using SetCursorPos_t = BOOL(WINAPI*)(int, int);
		using ClipCursor_t = BOOL(WINAPI*)(const RECT*);
		SetCursorPos_t original_SetCursorPos = nullptr;
		ClipCursor_t original_ClipCursor = nullptr;

		void UpdateAnyOpen()
		{
			bool any = false, visible = false;
			for (int i = 0; i < g_tool_count; i++)
			{
				any = any || (g_tools[i]->open && !g_tools[i]->passive);
				visible = visible || g_tools[i]->open;
			}
			g_any_visible = visible;
			const bool was = g_any_open.exchange(any);
			if (any && !was && original_ClipCursor)
				original_ClipCursor(nullptr); // let the cursor leave the rect the game confines it to
			if (!any && was && ImGui::GetCurrentContext())
			{
				std::lock_guard lock(g_imgui_mutex);
				ImGui::GetIO().AddFocusEvent(false); // releases any keys/buttons ImGui thinks are held
			}
		}

		// The close key: every interactive tool (passive ones follow the menu).
		void CloseAll()
		{
			for (int i = 0; i < g_tool_count; i++)
				if (!g_tools[i]->passive)
					g_tools[i]->open = false;
			UpdateAnyOpen();
		}

		// The game recentres and confines the cursor every frame; ignore that while a tool is open.
		BOOL WINAPI SetCursorPos_hook(int x, int y)
		{
			InHook guard;
			if (g_any_open && !g_shutdown)
				return TRUE;
			return original_SetCursorPos(x, y);
		}

		BOOL WINAPI ClipCursor_hook(const RECT* rect)
		{
			InHook guard;
			if (g_any_open && !g_shutdown && rect != nullptr)
				return TRUE;
			return original_ClipCursor(rect);
		}

		bool IsInputMessage(UINT msg)
		{
			return (msg >= WM_KEYFIRST && msg <= WM_KEYLAST) || (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) || msg == WM_INPUT;
		}

		LRESULT CALLBACK WndProc_hook(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
		{
			InHook guard;
			// The close key's key-up after its key-down closed the overlay: the
			// native menu acts on key-up, so it mustn't see this one.
			if ((msg == WM_KEYUP || msg == WM_SYSKEYUP) && (int)wparam == g_swallow_key_up)
			{
				g_swallow_key_up = 0;
				return 0;
			}
			if (g_shutdown || !g_any_open)
				return CallWindowProcW(g_original_wndproc, hwnd, msg, wparam, lparam);

			if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && (lparam & (1 << 30)) == 0 && (int)wparam == g_close_key)
			{
				g_swallow_key_up = (int)wparam;
				CloseAll();
				return 0;
			}
			{
				std::lock_guard lock(g_imgui_mutex);
				ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam);
			}
			if (IsInputMessage(msg))
			{
				// WM_INPUT must still go through DefWindowProc so the raw input buffer gets released.
				return msg == WM_INPUT ? DefWindowProcW(hwnd, msg, wparam, lparam) : 0;
			}
			return CallWindowProcW(g_original_wndproc, hwnd, msg, wparam, lparam);
		}

		void InstallCursorHooks()
		{
			HMODULE user32 = GetModuleHandleA("user32");
			CreateHook(reinterpret_cast<void*>(GetProcAddress(user32, "SetCursorPos")), reinterpret_cast<void*>(SetCursorPos_hook),
				reinterpret_cast<void**>(&original_SetCursorPos));
			CreateHook(reinterpret_cast<void*>(GetProcAddress(user32, "ClipCursor")), reinterpret_cast<void*>(ClipCursor_hook),
				reinterpret_cast<void**>(&original_ClipCursor));
			MH_ApplyQueued();
		}

		DWORD WINAPI InitThread(LPVOID)
		{
			const MH_STATUS status = MH_Initialize();
			if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED)
			{
				Log::Write("Overlay: MH_Initialize failed ({})", MH_StatusToString(status));
				g_init_running = false;
				return 0;
			}

			InstallCursorHooks();

			// Both renderers get hooked if both DLLs are there; Vulkan goes first because DX12 has
			// to give it time to show it's the real renderer.
			bool dx12_done = false;
			bool vulkan_done = false;
			while (!g_shutdown && !(dx12_done && vulkan_done) && ActiveBackend() == Backend::None)
			{
				if (!vulkan_done && GetModuleHandleA("vulkan-1.dll"))
				{
					vulkan_done = true;
					if (InstallVulkanHooks())
						g_vulkan_hooks_tick = GetTickCount64();
					else
						Log::Write("Overlay: Vulkan hooks not installed");
				}
				if (!dx12_done && GetModuleHandleA("d3d12.dll") && GetModuleHandleA("dxgi.dll"))
				{
					dx12_done = true;
					if (InstallDx12Hooks())
						g_dx12_hooks_tick = GetTickCount64();
					else
						Log::Write("Overlay: DX12 hooks not installed");
				}
				Sleep(50);
			}
			g_init_running = false;
			return 0;
		}

		void Start()
		{
			if (g_started)
				return;
			g_started = true;
			g_init_running = true;
			if (HANDLE thread = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr))
				CloseHandle(thread);
			else
				g_init_running = false;
		}
	}

	bool CreateHook(void* target, void* detour, void** original)
	{
		if (!target || MH_CreateHook(target, detour, original) != MH_OK)
			return false;
		if (MH_QueueEnableHook(target) != MH_OK)
		{
			MH_RemoveHook(target);
			return false;
		}
		TrackHook(target);
		return true;
	}

	void TrackHook(void* target)
	{
		std::lock_guard lock(g_hooks_mutex);
		g_hook_targets.push_back(target);
	}

	void UntrackHook(void* target)
	{
		std::lock_guard lock(g_hooks_mutex);
		std::erase(g_hook_targets, target);
	}

	std::recursive_mutex& ImGuiMutex()
	{
		return g_imgui_mutex;
	}

	bool ClaimBackend(Backend backend)
	{
		Backend expected = Backend::None;
		if (g_backend.compare_exchange_strong(expected, backend))
		{
			Log::Write("Overlay: rendering with {}", backend == Backend::DX12 ? "DX12" : "Vulkan");
			return true;
		}
		return expected == backend;
	}

	Backend ActiveBackend()
	{
		return g_backend;
	}

	const char* ActiveBackendName()
	{
		switch (g_backend.load())
		{
		case Backend::DX12: return "DX12";
		case Backend::Vulkan: return "Vulkan";
		default: return "None";
		}
	}

	bool Dx12MayClaim()
	{
		if (g_vulkan_present_seen)
			return false;
		if (ActiveBackend() == Backend::DX12)
			return true;
		const ULONGLONG since = (std::max)(g_dx12_hooks_tick.load(), g_vulkan_hooks_tick.load());
		return since != 0 && GetTickCount64() - since > kDx12ClaimDelayMs;
	}

	bool ShouldRender()
	{
		return g_any_visible && !g_shutdown;
	}

	HWND FindGameWindow()
	{
		struct Search { DWORD pid; HWND best; LONG best_area; } search = { GetCurrentProcessId(), nullptr, 0 };
		EnumWindows([](HWND hwnd, LPARAM param) -> BOOL
		{
			auto* s = reinterpret_cast<Search*>(param);
			DWORD pid = 0;
			GetWindowThreadProcessId(hwnd, &pid);
			if (pid != s->pid || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr)
				return TRUE;
			char class_name[64] = {};
			GetClassNameA(hwnd, class_name, sizeof(class_name));
			if (strcmp(class_name, "sgaWindow") == 0)
			{
				s->best = hwnd;
				return FALSE;
			}
			RECT rc;
			GetClientRect(hwnd, &rc);
			const LONG area = (rc.right - rc.left) * (rc.bottom - rc.top);
			if (area > s->best_area)
			{
				s->best = hwnd;
				s->best_area = area;
			}
			return TRUE;
		}, reinterpret_cast<LPARAM>(&search));
		return search.best;
	}

	bool EnsureContext(HWND hwnd)
	{
		if (ImGui::GetCurrentContext())
			return true;
		if (hwnd == nullptr)
			return false;

		std::lock_guard lock(g_imgui_mutex);

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.IniFilename = nullptr;
		io.LogFilename = nullptr;
		io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

		ImGui::StyleColorsDark();
		ImGuiStyle& style = ImGui::GetStyle();
		RECT rc;
		GetClientRect(hwnd, &rc);
		const float scale = (std::max)(1.0f, (rc.bottom - rc.top) / 1080.0f);
		style.ScaleAllSizes(scale);
		style.FontScaleMain = scale;
		style.WindowRounding = 6.0f * scale;
		style.FrameRounding = 4.0f * scale;

		if (!ImGui_ImplWin32_Init(hwnd))
		{
			ImGui::DestroyContext();
			return false;
		}

		g_hwnd = hwnd;
		g_original_wndproc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProc_hook)));
		return true;
	}

	void BuildFrame()
	{
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
		ImGui::GetIO().MouseDrawCursor = g_any_open;
		for (int i = 0; i < g_tool_count; i++)
		{
			Tool* tool = g_tools[i];
			if (!tool->open)
				continue;
			bool open = true;
			tool->draw(&open);
			if (!open)
			{
				tool->open = false;
				UpdateAnyOpen();
			}
		}
		ImGui::Render();
	}

	void Register(Tool& tool)
	{
		const int count = g_tool_count;
		if (count < kMaxTools)
		{
			g_tools[count] = &tool;
			g_tool_count = count + 1;
		}
	}

	void SetOpen(Tool& tool, bool open)
	{
		if (open)
			Start();
		tool.open = open;
		UpdateAnyOpen();
	}

	bool AnyOpen()
	{
		return g_any_open;
	}

	void SetCloseKey(int virtualKey)
	{
		g_close_key = virtualKey;
	}

	void Shutdown()
	{
		if (!g_started)
			return;
		g_shutdown = true;
		g_any_open = false;
		g_any_visible = false;

		// The init thread checks g_shutdown every 50 ms.
		for (int i = 0; i < 40 && g_init_running; i++)
			Sleep(25);

		if (g_hwnd && g_original_wndproc)
			SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_original_wndproc));
		{
			std::lock_guard lock(g_hooks_mutex);
			for (void* target : g_hook_targets)
				MH_DisableHook(target);
			for (void* target : g_hook_targets)
				MH_RemoveHook(target);
			g_hook_targets.clear();
		}

		// A present or window message can still be inside a hook body.
		for (int i = 0; i < 100 && InHook::count > 0; i++)
			Sleep(10);
		if (InHook::count > 0)
		{
			Log::Write("Overlay: a renderer thread is still in a hook; leaving ImGui allocated");
			return;
		}

		ShutdownDx12();
		ShutdownVulkan();
		std::lock_guard lock(g_imgui_mutex);
		if (ImGui::GetCurrentContext())
		{
			ImGui_ImplWin32_Shutdown();
			ImGui::DestroyContext();
		}
	}
}

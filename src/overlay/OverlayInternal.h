/*
	Shared between the overlay's renderer backends (OverlayDx12.cpp,
	OverlayVulkan.cpp) and Overlay.cpp.
*/

#pragma once

#include "Overlay.h"

#include <windows.h>

#include <atomic>
#include <mutex>

namespace Overlay
{
	enum class Backend { None, DX12, Vulkan };

	extern std::atomic<bool> g_shutdown;

	// The first backend to initialise successfully owns the overlay; the other stays passive.
	// Vulkan presenting through a DXGI swapchain (NVIDIA "layered" present) would otherwise draw twice.
	bool ClaimBackend(Backend backend);
	Backend ActiveBackend();

	// Set by the Vulkan present hook. The DX12 hook stays passive once it is seen.
	extern std::atomic<bool> g_vulkan_present_seen;

	// d3d12.dll and vulkan-1.dll are both loaded in either mode, and in Vulkan mode a D3D12 swapchain
	// (NVIDIA's DXGI present path) presents too. Only a vkQueuePresentKHR call tells the modes
	// apart, so DX12 may only take the overlay after Vulkan had a few seconds to show one.
	bool Dx12MayClaim();

	// Creates the ImGui context, Win32 backend and WndProc hook on first call.
	bool EnsureContext(HWND hwnd);
	HWND FindGameWindow();

	// Whether anything needs drawing this frame.
	bool ShouldRender();

	// Guards ImGui state between the render thread and the window thread.
	std::recursive_mutex& ImGuiMutex();

	// Win32 + ImGui NewFrame, draws the tools, ImGui::Render. Caller holds ImGuiMutex()
	// and has already called its renderer backend's NewFrame.
	void BuildFrame();

	// Counts threads inside one of our hooks, so Shutdown can wait for them to leave.
	struct InHook
	{
		static std::atomic<int> count;
		InHook() { count++; }
		~InHook() { count--; }
	};

	// MinHook bookkeeping: every enabled target is tracked so Shutdown can remove them all.
	// CreateHook creates, queues and tracks; backends that create their own call TrackHook.
	bool CreateHook(void* target, void* detour, void** original);
	void TrackHook(void* target);
	void UntrackHook(void* target);

	bool InstallDx12Hooks();
	bool InstallVulkanHooks();
	// Release the backend's objects; only after every hook is gone and InHook::count is 0.
	void ShutdownDx12();
	void ShutdownVulkan();
}

#include "OverlayInternal.h"
#include "..\Log.h"

#include "MinHook.h"

#include "imgui.h"
#include "backends/imgui_impl_dx12.h"

#include <d3d12.h>
#include <dxgi1_4.h>

#include <vector>

namespace Overlay
{
	// vtable slots (see the C Vtbl structs in dxgi1_4.h / d3d12.h)
	static constexpr int kPresentIndex = 8;            // IDXGISwapChain::Present
	static constexpr int kResizeBuffersIndex = 13;     // IDXGISwapChain::ResizeBuffers
	static constexpr int kPresent1Index = 22;          // IDXGISwapChain1::Present1
	static constexpr int kResizeBuffers1Index = 39;    // IDXGISwapChain3::ResizeBuffers1
	static constexpr int kExecuteCommandListsIndex = 10; // ID3D12CommandQueue::ExecuteCommandLists

	static constexpr UINT kSrvHeapSize = 64;

	using Present_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
	using Present1_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
	using ResizeBuffers_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
	using ResizeBuffers1_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT, const UINT*, IUnknown* const*);
	using ExecuteCommandLists_t = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);

	static Present_t original_Present = nullptr;
	static Present1_t original_Present1 = nullptr;
	static ResizeBuffers_t original_ResizeBuffers = nullptr;
	static ResizeBuffers1_t original_ResizeBuffers1 = nullptr;
	static ExecuteCommandLists_t original_ExecuteCommandLists = nullptr;

	struct Dx12Frame
	{
		ID3D12CommandAllocator* allocator = nullptr;
		ID3D12Resource* back_buffer = nullptr;
		D3D12_CPU_DESCRIPTOR_HANDLE rtv = {};
		UINT64 fence_value = 0;
	};

	static std::mutex dx12_mutex;
	static bool dx12_disabled = false;
	static ID3D12Device* dx12_device = nullptr;
	static ID3D12CommandQueue* dx12_queue = nullptr;
	static ID3D12DescriptorHeap* dx12_rtv_heap = nullptr;
	static ID3D12DescriptorHeap* dx12_srv_heap = nullptr;
	static ID3D12GraphicsCommandList* dx12_command_list = nullptr;
	static ID3D12Fence* dx12_fence = nullptr;
	static HANDLE dx12_fence_event = nullptr;
	static UINT64 dx12_fence_counter = 0;
	static IDXGISwapChain3* dx12_swapchain = nullptr;
	static IDXGISwapChain* dx12_swapchain_key = nullptr; // pointer the game presents with
	static std::vector<Dx12Frame> dx12_frames;
	static DXGI_FORMAT dx12_backend_format = DXGI_FORMAT_UNKNOWN;
	static UINT dx12_backend_frames = 0;
	static bool dx12_backend_ready = false;

	// The game's direct queue. We don't know which one owns the swapchain, so prefer the one the
	// presenting thread submitted to last, falling back to the first direct queue seen.
	static std::atomic<ID3D12CommandQueue*> first_direct_queue = nullptr;
	static thread_local ID3D12CommandQueue* last_direct_queue_on_thread = nullptr;

	// Simple free-list allocator over the shader-visible SRV heap, handed to the ImGui backend.
	static std::vector<UINT> srv_free_list;
	static UINT srv_descriptor_size = 0;

	static void SrvAlloc(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu)
	{
		IM_ASSERT(!srv_free_list.empty());
		UINT index = srv_free_list.back();
		srv_free_list.pop_back();
		out_cpu->ptr = dx12_srv_heap->GetCPUDescriptorHandleForHeapStart().ptr + (SIZE_T)index * srv_descriptor_size;
		out_gpu->ptr = dx12_srv_heap->GetGPUDescriptorHandleForHeapStart().ptr + (UINT64)index * srv_descriptor_size;
	}

	static void SrvFree(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE)
	{
		UINT index = (UINT)((cpu.ptr - dx12_srv_heap->GetCPUDescriptorHandleForHeapStart().ptr) / srv_descriptor_size);
		srv_free_list.push_back(index);
	}

	template <typename T>
	static void SafeRelease(T*& p)
	{
		if (p)
		{
			p->Release();
			p = nullptr;
		}
	}

	static void WaitForGpu()
	{
		if (!dx12_queue || !dx12_fence)
		{
			return;
		}
		const UINT64 value = ++dx12_fence_counter;
		if (SUCCEEDED(dx12_queue->Signal(dx12_fence, value)) && dx12_fence->GetCompletedValue() < value)
		{
			dx12_fence->SetEventOnCompletion(value, dx12_fence_event);
			WaitForSingleObject(dx12_fence_event, 2000);
		}
	}

	static void ReleaseSwapchainResources()
	{
		WaitForGpu();
		for (auto& frame : dx12_frames)
		{
			SafeRelease(frame.back_buffer);
			SafeRelease(frame.allocator);
		}
		dx12_frames.clear();
		SafeRelease(dx12_rtv_heap);
		SafeRelease(dx12_swapchain);
		dx12_swapchain_key = nullptr;
	}

	static bool CreateSwapchainResources(IDXGISwapChain* swapchain)
	{
		ReleaseSwapchainResources();

		if (FAILED(swapchain->QueryInterface(IID_PPV_ARGS(&dx12_swapchain))))
		{
			return false;
		}
		dx12_swapchain_key = swapchain;

		DXGI_SWAP_CHAIN_DESC desc = {};
		dx12_swapchain->GetDesc(&desc);

		D3D12_DESCRIPTOR_HEAP_DESC rtv_desc = {};
		rtv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtv_desc.NumDescriptors = desc.BufferCount;
		if (FAILED(dx12_device->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&dx12_rtv_heap))))
		{
			return false;
		}

		const UINT rtv_size = dx12_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		D3D12_CPU_DESCRIPTOR_HANDLE rtv = dx12_rtv_heap->GetCPUDescriptorHandleForHeapStart();
		dx12_frames.resize(desc.BufferCount);
		for (UINT i = 0; i < desc.BufferCount; i++)
		{
			Dx12Frame& frame = dx12_frames[i];
			if (FAILED(dx12_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frame.allocator))) ||
				FAILED(dx12_swapchain->GetBuffer(i, IID_PPV_ARGS(&frame.back_buffer))))
			{
				return false;
			}
			frame.rtv = rtv;
			D3D12_RENDER_TARGET_VIEW_DESC view = {};
			view.Format = desc.BufferDesc.Format;
			view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
			dx12_device->CreateRenderTargetView(frame.back_buffer, &view, frame.rtv);
			rtv.ptr += rtv_size;
		}

		if (!dx12_command_list)
		{
			if (FAILED(dx12_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, dx12_frames[0].allocator, nullptr, IID_PPV_ARGS(&dx12_command_list))))
			{
				return false;
			}
			dx12_command_list->Close();
		}

		// (Re)initialise the ImGui renderer if the pipeline format or frame count changed.
		if (dx12_backend_ready && (dx12_backend_format != desc.BufferDesc.Format || dx12_backend_frames != desc.BufferCount))
		{
			ImGui_ImplDX12_Shutdown();
			dx12_backend_ready = false;
		}
		if (!dx12_backend_ready)
		{
			if (!EnsureContext(desc.OutputWindow))
			{
				return false;
			}
			ImGui_ImplDX12_InitInfo init_info;
			init_info.Device = dx12_device;
			init_info.CommandQueue = dx12_queue;
			init_info.NumFramesInFlight = (int)desc.BufferCount;
			init_info.RTVFormat = desc.BufferDesc.Format;
			init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;
			init_info.SrvDescriptorHeap = dx12_srv_heap;
			init_info.SrvDescriptorAllocFn = SrvAlloc;
			init_info.SrvDescriptorFreeFn = SrvFree;
			std::lock_guard lock(ImGuiMutex());
			if (!ImGui_ImplDX12_Init(&init_info))
			{
				return false;
			}
			dx12_backend_ready = true;
			dx12_backend_format = desc.BufferDesc.Format;
			dx12_backend_frames = desc.BufferCount;
		}
		return true;
	}

	static bool CreateDeviceResources(IDXGISwapChain* swapchain)
	{
		if (FAILED(swapchain->GetDevice(IID_PPV_ARGS(&dx12_device))))
		{
			// Not a D3D12 swapchain (e.g. Vulkan presenting through DXGI); nothing for us to do here.
			return false;
		}

		ID3D12CommandQueue* queue = last_direct_queue_on_thread ? last_direct_queue_on_thread : first_direct_queue.load();
		if (!queue)
		{
			SafeRelease(dx12_device);
			return false; // try again next frame, once the game has submitted work
		}
		ID3D12Device* queue_device = nullptr;
		if (FAILED(queue->GetDevice(IID_PPV_ARGS(&queue_device))) || queue_device != dx12_device)
		{
			SafeRelease(queue_device);
			SafeRelease(dx12_device);
			return false;
		}
		SafeRelease(queue_device);
		queue->AddRef();
		dx12_queue = queue;

		D3D12_DESCRIPTOR_HEAP_DESC srv_desc = {};
		srv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		srv_desc.NumDescriptors = kSrvHeapSize;
		srv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if (FAILED(dx12_device->CreateDescriptorHeap(&srv_desc, IID_PPV_ARGS(&dx12_srv_heap))))
		{
			return false;
		}
		srv_descriptor_size = dx12_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		for (UINT i = kSrvHeapSize; i > 0; i--)
		{
			srv_free_list.push_back(i - 1);
		}

		if (FAILED(dx12_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&dx12_fence))))
		{
			return false;
		}
		dx12_fence_event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
		return dx12_fence_event != nullptr;
	}

	static void RenderDx12(IDXGISwapChain* swapchain)
	{
		// Don't even set up until we know the game really renders with DX12 (see Dx12MayClaim).
		if (g_shutdown || ActiveBackend() == Backend::Vulkan || !Dx12MayClaim())
		{
			return;
		}

		std::lock_guard lock(dx12_mutex);
		if (dx12_disabled)
		{
			return;
		}

		if (!dx12_device)
		{
			if (!CreateDeviceResources(swapchain))
			{
				if (dx12_device) // device was fine but setup failed, give up
				{
					dx12_disabled = true;
					Log::Write("Overlay: DX12 device setup failed");
				}
				return;
			}
		}

		if (swapchain != dx12_swapchain_key)
		{
			if (!CreateSwapchainResources(swapchain))
			{
				dx12_disabled = true;
				Log::Write("Overlay: DX12 swapchain setup failed");
				return;
			}
		}

		if (!ClaimBackend(Backend::DX12) || !ShouldRender())
		{
			return;
		}

		Dx12Frame& frame = dx12_frames[dx12_swapchain->GetCurrentBackBufferIndex() % dx12_frames.size()];
		if (dx12_fence->GetCompletedValue() < frame.fence_value)
		{
			dx12_fence->SetEventOnCompletion(frame.fence_value, dx12_fence_event);
			WaitForSingleObject(dx12_fence_event, 2000);
		}

		std::lock_guard imgui_lock(ImGuiMutex());
		ImGui_ImplDX12_NewFrame();
		BuildFrame();

		frame.allocator->Reset();
		dx12_command_list->Reset(frame.allocator, nullptr);

		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = frame.back_buffer;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		dx12_command_list->ResourceBarrier(1, &barrier);

		dx12_command_list->OMSetRenderTargets(1, &frame.rtv, FALSE, nullptr);
		dx12_command_list->SetDescriptorHeaps(1, &dx12_srv_heap);
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), dx12_command_list);

		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
		dx12_command_list->ResourceBarrier(1, &barrier);
		dx12_command_list->Close();

		ID3D12CommandList* lists[] = { dx12_command_list };
		original_ExecuteCommandLists(dx12_queue, 1, lists);
		frame.fence_value = ++dx12_fence_counter;
		dx12_queue->Signal(dx12_fence, frame.fence_value);
	}

	static HRESULT STDMETHODCALLTYPE Present_hook(IDXGISwapChain* swapchain, UINT sync_interval, UINT flags)
	{
		InHook guard;
		if (!(flags & DXGI_PRESENT_TEST))
		{
			RenderDx12(swapchain);
		}
		return original_Present(swapchain, sync_interval, flags);
	}

	static HRESULT STDMETHODCALLTYPE Present1_hook(IDXGISwapChain1* swapchain, UINT sync_interval, UINT flags, const DXGI_PRESENT_PARAMETERS* params)
	{
		InHook guard;
		if (!(flags & DXGI_PRESENT_TEST))
		{
			RenderDx12(swapchain);
		}
		return original_Present1(swapchain, sync_interval, flags, params);
	}

	// The back buffers can't be resized while we hold references to them.
	static void OnResize(IDXGISwapChain* swapchain)
	{
		std::lock_guard lock(dx12_mutex);
		if (swapchain == dx12_swapchain_key)
		{
			ReleaseSwapchainResources();
		}
	}

	static HRESULT STDMETHODCALLTYPE ResizeBuffers_hook(IDXGISwapChain* swapchain, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags)
	{
		InHook guard;
		OnResize(swapchain);
		return original_ResizeBuffers(swapchain, count, width, height, format, flags);
	}

	static HRESULT STDMETHODCALLTYPE ResizeBuffers1_hook(IDXGISwapChain3* swapchain, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags, const UINT* node_masks, IUnknown* const* queues)
	{
		InHook guard;
		OnResize(swapchain);
		return original_ResizeBuffers1(swapchain, count, width, height, format, flags, node_masks, queues);
	}

	static void STDMETHODCALLTYPE ExecuteCommandLists_hook(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists)
	{
		InHook guard;
		if (!dx12_queue && queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT)
		{
			last_direct_queue_on_thread = queue;
			ID3D12CommandQueue* expected = nullptr;
			first_direct_queue.compare_exchange_strong(expected, queue);
		}
		original_ExecuteCommandLists(queue, count, lists);
	}

	static std::vector<LPVOID> created_hooks;

	static bool HookVtable(void** vtable, int index, LPVOID detour, LPVOID* original)
	{
		if (MH_CreateHook(vtable[index], detour, original) != MH_OK)
		{
			return false;
		}
		created_hooks.push_back(vtable[index]);
		return MH_QueueEnableHook(vtable[index]) == MH_OK;
	}

	bool InstallDx12Hooks()
	{
		using D3D12CreateDevice_t = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
		using CreateDXGIFactory1_t = HRESULT(WINAPI*)(REFIID, void**);
		auto create_device = reinterpret_cast<D3D12CreateDevice_t>(GetProcAddress(GetModuleHandleA("d3d12.dll"), "D3D12CreateDevice"));
		auto create_factory = reinterpret_cast<CreateDXGIFactory1_t>(GetProcAddress(GetModuleHandleA("dxgi.dll"), "CreateDXGIFactory1"));
		if (!create_device || !create_factory)
		{
			return false;
		}

		// Build a throwaway device + swapchain just to read the vtables.
		WNDCLASSEXA wc = { sizeof(wc), CS_HREDRAW | CS_VREDRAW, DefWindowProcA, 0, 0, GetModuleHandleA(nullptr), nullptr, nullptr, nullptr, nullptr, "RAMPAGIO_DX12_Dummy", nullptr };
		RegisterClassExA(&wc);
		HWND hwnd = CreateWindowA(wc.lpszClassName, "", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);

		bool ok = false;
		IDXGIFactory2* factory = nullptr;
		ID3D12Device* device = nullptr;
		ID3D12CommandQueue* queue = nullptr;
		IDXGISwapChain1* swapchain = nullptr;
		if (hwnd &&
			SUCCEEDED(create_factory(IID_PPV_ARGS(&factory))) &&
			SUCCEEDED(create_device(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
		{
			D3D12_COMMAND_QUEUE_DESC queue_desc = {};
			queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
			DXGI_SWAP_CHAIN_DESC1 sc_desc = {};
			sc_desc.Width = 100;
			sc_desc.Height = 100;
			sc_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			sc_desc.SampleDesc.Count = 1;
			sc_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			sc_desc.BufferCount = 2;
			sc_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
			if (SUCCEEDED(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))) &&
				SUCCEEDED(factory->CreateSwapChainForHwnd(queue, hwnd, &sc_desc, nullptr, nullptr, &swapchain)))
			{
				void** sc_vtable = *reinterpret_cast<void***>(swapchain);
				void** queue_vtable = *reinterpret_cast<void***>(queue);
				ok = HookVtable(sc_vtable, kPresentIndex, reinterpret_cast<LPVOID>(Present_hook), reinterpret_cast<LPVOID*>(&original_Present)) &&
					HookVtable(sc_vtable, kResizeBuffersIndex, reinterpret_cast<LPVOID>(ResizeBuffers_hook), reinterpret_cast<LPVOID*>(&original_ResizeBuffers)) &&
					HookVtable(queue_vtable, kExecuteCommandListsIndex, reinterpret_cast<LPVOID>(ExecuteCommandLists_hook), reinterpret_cast<LPVOID*>(&original_ExecuteCommandLists));
				// Optional entry points; not every swapchain exposes them.
				HookVtable(sc_vtable, kPresent1Index, reinterpret_cast<LPVOID>(Present1_hook), reinterpret_cast<LPVOID*>(&original_Present1));
				IDXGISwapChain3* swapchain3 = nullptr;
				if (SUCCEEDED(swapchain->QueryInterface(IID_PPV_ARGS(&swapchain3))))
				{
					HookVtable(*reinterpret_cast<void***>(swapchain3), kResizeBuffers1Index, reinterpret_cast<LPVOID>(ResizeBuffers1_hook), reinterpret_cast<LPVOID*>(&original_ResizeBuffers1));
					swapchain3->Release();
				}
			}
		}

		SafeRelease(swapchain);
		SafeRelease(queue);
		SafeRelease(device);
		SafeRelease(factory);
		if (hwnd)
		{
			DestroyWindow(hwnd);
		}
		UnregisterClassA(wc.lpszClassName, wc.hInstance);

		if (!ok)
		{
			for (LPVOID target : created_hooks)
			{
				MH_RemoveHook(target);
			}
			created_hooks.clear();
			return false;
		}
		for (LPVOID target : created_hooks)
		{
			TrackHook(target);
		}
		created_hooks.clear();
		MH_ApplyQueued();
		Log::Write("Overlay: DX12 hooks installed");
		return true;
	}
}

namespace Overlay
{
	void ShutdownDx12()
	{
		std::lock_guard lock(dx12_mutex);
		if (!dx12_device)
		{
			return;
		}
		ReleaseSwapchainResources(); // waits for the GPU first
		if (dx12_backend_ready)
		{
			std::lock_guard imgui_lock(ImGuiMutex());
			ImGui_ImplDX12_Shutdown();
			dx12_backend_ready = false;
		}
		SafeRelease(dx12_command_list);
		SafeRelease(dx12_srv_heap);
		SafeRelease(dx12_fence);
		if (dx12_fence_event)
		{
			CloseHandle(dx12_fence_event);
			dx12_fence_event = nullptr;
		}
		SafeRelease(dx12_queue);
		SafeRelease(dx12_device);
		srv_free_list.clear();
	}
}

#include "OverlayInternal.h"
#include "..\Log.h"

#include "MinHook.h"

#include "imgui.h"
#include "backends/imgui_impl_vulkan.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

// How this works:
// - We create our own VkInstance and a throwaway VkDevice on the same GPU, and ask it for the device-level
//   entry points (vkQueuePresentKHR etc). Those resolve to the same driver/layer code the game's device
//   uses, so hooking them catches the game's calls whether it goes through vulkan-1.dll or not.
// - vkAcquireNextImageKHR gives us the game's VkDevice; vkCreateSwapchainKHR gives us the swapchain format.
// - In vkQueuePresentKHR we render ImGui into the image being presented, waiting on the game's semaphores
//   and handing our own semaphore to the real present.
namespace Overlay
{
#define RAMPAGIO_VK_DEVICE_FUNCS(X) \
	X(vkGetSwapchainImagesKHR) \
	X(vkCreateImageView) \
	X(vkDestroyImageView) \
	X(vkCreateRenderPass) \
	X(vkDestroyRenderPass) \
	X(vkCreateFramebuffer) \
	X(vkDestroyFramebuffer) \
	X(vkCreateCommandPool) \
	X(vkAllocateCommandBuffers) \
	X(vkFreeCommandBuffers) \
	X(vkCreateFence) \
	X(vkDestroyFence) \
	X(vkCreateSemaphore) \
	X(vkDestroySemaphore) \
	X(vkWaitForFences) \
	X(vkResetFences) \
	X(vkResetCommandBuffer) \
	X(vkBeginCommandBuffer) \
	X(vkEndCommandBuffer) \
	X(vkCmdBeginRenderPass) \
	X(vkCmdEndRenderPass) \
	X(vkQueueSubmit)

	namespace vkfn
	{
		static PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
		static PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr;
		static PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties;
#define RAMPAGIO_VK_DECLARE(name) static PFN_##name name;
		RAMPAGIO_VK_DEVICE_FUNCS(RAMPAGIO_VK_DECLARE)
#undef RAMPAGIO_VK_DECLARE
	}

	static PFN_vkQueuePresentKHR original_vkQueuePresentKHR = nullptr;
	static PFN_vkAcquireNextImageKHR original_vkAcquireNextImageKHR = nullptr;
	static PFN_vkAcquireNextImage2KHR original_vkAcquireNextImage2KHR = nullptr;
	static PFN_vkCreateSwapchainKHR original_vkCreateSwapchainKHR = nullptr;
	static PFN_vkDestroySwapchainKHR original_vkDestroySwapchainKHR = nullptr;
	static PFN_vkCreateDevice original_vkCreateDevice = nullptr;

	struct VkSwapchainInfo
	{
		VkFormat format;
		VkExtent2D extent;
		uint32_t min_image_count;
	};

	struct VkDeviceInfo
	{
		VkPhysicalDevice physical_device;
		uint32_t graphics_family;
	};

	struct VkFrame
	{
		VkImage image = VK_NULL_HANDLE;
		VkImageView view = VK_NULL_HANDLE;
		VkFramebuffer framebuffer = VK_NULL_HANDLE;
		VkCommandBuffer command_buffer = VK_NULL_HANDLE;
		VkFence fence = VK_NULL_HANDLE;
		VkSemaphore render_done = VK_NULL_HANDLE;
	};

	// Only the swapchain/device tables are touched from other threads, under vk_info_mutex.
	static std::mutex vk_info_mutex;
	static std::unordered_map<VkSwapchainKHR, VkSwapchainInfo> vk_swapchain_infos;
	static std::unordered_map<VkDevice, VkDeviceInfo> vk_device_infos;
	static std::atomic<VkDevice> vk_game_device = VK_NULL_HANDLE;

	// Render state, under vk_mutex.
	static std::mutex vk_mutex;
	static bool vk_disabled = false;
	static VkInstance vk_instance = VK_NULL_HANDLE;           // ours
	static VkPhysicalDevice vk_hooked_physical = VK_NULL_HANDLE; // GPU whose entry points we hooked
	static VkDevice vk_device = VK_NULL_HANDLE;               // the game's, once we start rendering
	static VkPhysicalDevice vk_physical = VK_NULL_HANDLE;
	static uint32_t vk_queue_family = 0;
	static VkCommandPool vk_command_pool = VK_NULL_HANDLE;
	static VkRenderPass vk_render_pass = VK_NULL_HANDLE;
	static VkSwapchainKHR vk_swapchain = VK_NULL_HANDLE;
	static VkExtent2D vk_extent = {};
	static std::vector<VkFrame> vk_frames;
	static bool vk_backend_ready = false;
	static VkFormat vk_backend_format = VK_FORMAT_UNDEFINED;
	static uint32_t vk_backend_image_count = 0;

	// When the ASI loads after the game set up Vulkan, we never saw the swapchain's create info.
	// Nudge the game into recreating it (the normal resize path) so vkCreateSwapchainKHR_hook sees it:
	// first a harmless VK_SUBOPTIMAL_KHR from present, then VK_ERROR_OUT_OF_DATE_KHR from acquire,
	// and only if both are ignored fall back to guessing the format.
	static constexpr uint32_t kSuboptimalAtFrame = 1;
	static constexpr uint32_t kOutOfDateAtFrame = 60;
	static constexpr uint32_t kGuessAtFrame = 180;
	static uint32_t vk_unknown_swapchain_frames = 0; // under vk_mutex
	static std::atomic<bool> vk_force_suboptimal = false;
	static std::atomic<bool> vk_force_out_of_date = false;

	// One-shot diagnostics so a failed run shows how far the Vulkan path got.
#define RAMPAGIO_LOG_ONCE(...) do { static std::atomic<bool> logged = false; if (!logged.exchange(true)) Log::Write(__VA_ARGS__); } while (0)

	static bool IsKnownSwapchain(VkSwapchainKHR swapchain)
	{
		std::lock_guard lock(vk_info_mutex);
		return vk_swapchain_infos.contains(swapchain);
	}

	static uint32_t FindGraphicsFamily(VkPhysicalDevice physical_device)
	{
		uint32_t count = 0;
		vkfn::vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, nullptr);
		std::vector<VkQueueFamilyProperties> families(count);
		vkfn::vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, families.data());
		for (uint32_t i = 0; i < count; i++)
		{
			if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
			{
				return i;
			}
		}
		return 0;
	}

	static void DestroyFrames()
	{
		for (auto& frame : vk_frames)
		{
			if (frame.fence)
			{
				vkfn::vkWaitForFences(vk_device, 1, &frame.fence, VK_TRUE, 2'000'000'000ull);
				vkfn::vkDestroyFence(vk_device, frame.fence, nullptr);
			}
			if (frame.render_done) vkfn::vkDestroySemaphore(vk_device, frame.render_done, nullptr);
			if (frame.framebuffer) vkfn::vkDestroyFramebuffer(vk_device, frame.framebuffer, nullptr);
			if (frame.view) vkfn::vkDestroyImageView(vk_device, frame.view, nullptr);
			if (frame.command_buffer) vkfn::vkFreeCommandBuffers(vk_device, vk_command_pool, 1, &frame.command_buffer);
		}
		vk_frames.clear();
		vk_swapchain = VK_NULL_HANDLE;
	}

	static PFN_vkVoidFunction LoadImGuiFunction(const char* name, void*)
	{
		if (PFN_vkVoidFunction fn = vkfn::vkGetDeviceProcAddr(vk_device, name))
		{
			return fn;
		}
		return vkfn::vkGetInstanceProcAddr(vk_instance, name);
	}

	// Picks up the game's device the first time we render: loads our function table and the queue family.
	static bool BindDevice(VkDevice device)
	{
		vk_device = device;
#define RAMPAGIO_VK_LOAD(name) vkfn::name = reinterpret_cast<PFN_##name>(vkfn::vkGetDeviceProcAddr(device, #name)); if (!vkfn::name) { Log::Write("Overlay: Vulkan function " #name " unavailable"); return false; }
		RAMPAGIO_VK_DEVICE_FUNCS(RAMPAGIO_VK_LOAD)
#undef RAMPAGIO_VK_LOAD

		{
			std::lock_guard lock(vk_info_mutex);
			auto it = vk_device_infos.find(device);
			if (it != vk_device_infos.end())
			{
				vk_physical = it->second.physical_device;
				vk_queue_family = it->second.graphics_family;
			}
			else
			{
				// The game created its device before we were loaded. Assume it's on the GPU we hooked.
				vk_physical = vk_hooked_physical;
				vk_queue_family = FindGraphicsFamily(vk_physical);
				Log::Write("Overlay: Vulkan device was created before hooks; assuming queue family {}", vk_queue_family);
			}
		}

		VkCommandPoolCreateInfo pool_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
		pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		pool_info.queueFamilyIndex = vk_queue_family;
		if (vkfn::vkCreateCommandPool(device, &pool_info, nullptr, &vk_command_pool) != VK_SUCCESS)
		{
			return false;
		}

		return ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_0, LoadImGuiFunction);
	}

	static bool CreateRenderPass(VkFormat format)
	{
		if (vk_render_pass)
		{
			vkfn::vkDestroyRenderPass(vk_device, vk_render_pass, nullptr);
			vk_render_pass = VK_NULL_HANDLE;
		}

		// Draw on top of what the game rendered, and leave the image ready to present.
		VkAttachmentDescription attachment = {};
		attachment.format = format;
		attachment.samples = VK_SAMPLE_COUNT_1_BIT;
		attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachment.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		VkAttachmentReference color_ref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
		VkSubpassDescription subpass = {};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &color_ref;

		VkSubpassDependency dependency = {};
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;
		dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.srcAccessMask = 0;
		dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		VkRenderPassCreateInfo info = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
		info.attachmentCount = 1;
		info.pAttachments = &attachment;
		info.subpassCount = 1;
		info.pSubpasses = &subpass;
		info.dependencyCount = 1;
		info.pDependencies = &dependency;
		return vkfn::vkCreateRenderPass(vk_device, &info, nullptr, &vk_render_pass) == VK_SUCCESS;
	}

	static bool SetupSwapchain(VkSwapchainKHR swapchain, VkQueue queue)
	{
		DestroyFrames();

		VkSwapchainInfo info = {};
		bool known = false;
		{
			std::lock_guard lock(vk_info_mutex);
			auto it = vk_swapchain_infos.find(swapchain);
			if (it != vk_swapchain_infos.end())
			{
				info = it->second;
				known = true;
			}
		}
		if (!known)
		{
			// Swapchain predates our hooks, so we never saw its create info. Fall back to the usual SDR
			// format and the window size; changing resolution or window mode resyncs it properly.
			RECT rc = {};
			GetClientRect(FindGameWindow(), &rc);
			info.format = VK_FORMAT_B8G8R8A8_UNORM;
			info.extent = { (uint32_t)(rc.right - rc.left), (uint32_t)(rc.bottom - rc.top) };
			info.min_image_count = 2;
			Log::Write("Overlay: Vulkan swapchain was created before hooks; assuming B8G8R8A8_UNORM {}x{}", info.extent.width, info.extent.height);
		}

		uint32_t image_count = 0;
		vkfn::vkGetSwapchainImagesKHR(vk_device, swapchain, &image_count, nullptr);
		std::vector<VkImage> images(image_count);
		if (image_count == 0 || vkfn::vkGetSwapchainImagesKHR(vk_device, swapchain, &image_count, images.data()) != VK_SUCCESS)
		{
			return false;
		}

		const bool format_changed = !vk_backend_ready || vk_backend_format != info.format;
		if (format_changed && !CreateRenderPass(info.format))
		{
			return false;
		}

		vk_frames.resize(image_count);
		for (uint32_t i = 0; i < image_count; i++)
		{
			VkFrame& frame = vk_frames[i];
			frame.image = images[i];

			VkImageViewCreateInfo view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
			view_info.image = frame.image;
			view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
			view_info.format = info.format;
			view_info.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			if (vkfn::vkCreateImageView(vk_device, &view_info, nullptr, &frame.view) != VK_SUCCESS)
			{
				return false;
			}

			VkFramebufferCreateInfo fb_info = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
			fb_info.renderPass = vk_render_pass;
			fb_info.attachmentCount = 1;
			fb_info.pAttachments = &frame.view;
			fb_info.width = info.extent.width;
			fb_info.height = info.extent.height;
			fb_info.layers = 1;
			if (vkfn::vkCreateFramebuffer(vk_device, &fb_info, nullptr, &frame.framebuffer) != VK_SUCCESS)
			{
				return false;
			}

			VkCommandBufferAllocateInfo cb_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
			cb_info.commandPool = vk_command_pool;
			cb_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			cb_info.commandBufferCount = 1;
			if (vkfn::vkAllocateCommandBuffers(vk_device, &cb_info, &frame.command_buffer) != VK_SUCCESS)
			{
				return false;
			}

			VkFenceCreateInfo fence_info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
			fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
			VkSemaphoreCreateInfo sem_info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
			if (vkfn::vkCreateFence(vk_device, &fence_info, nullptr, &frame.fence) != VK_SUCCESS ||
				vkfn::vkCreateSemaphore(vk_device, &sem_info, nullptr, &frame.render_done) != VK_SUCCESS)
			{
				return false;
			}
		}

		// The ImGui pipeline is tied to the render pass format and its buffers to the image count.
		if (vk_backend_ready && (format_changed || vk_backend_image_count != image_count))
		{
			std::lock_guard lock(ImGuiMutex());
			ImGui_ImplVulkan_Shutdown();
			vk_backend_ready = false;
		}
		if (!vk_backend_ready)
		{
			if (!EnsureContext(FindGameWindow()))
			{
				return false;
			}
			ImGui_ImplVulkan_InitInfo init_info = {};
			init_info.ApiVersion = VK_API_VERSION_1_0;
			init_info.Instance = vk_instance;
			init_info.PhysicalDevice = vk_physical;
			init_info.Device = vk_device;
			init_info.QueueFamily = vk_queue_family;
			init_info.Queue = queue;
			init_info.DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE + IMGUI_IMPL_VULKAN_MINIMUM_SAMPLER_POOL_SIZE;
			init_info.MinImageCount = (std::max)(2u, info.min_image_count);
			init_info.ImageCount = (std::max)(init_info.MinImageCount, image_count);
			init_info.PipelineInfoMain.RenderPass = vk_render_pass;
			init_info.PipelineInfoMain.Subpass = 0;
			init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
			std::lock_guard lock(ImGuiMutex());
			if (!ImGui_ImplVulkan_Init(&init_info))
			{
				return false;
			}
			vk_backend_ready = true;
			vk_backend_format = info.format;
			vk_backend_image_count = image_count;
		}

		vk_swapchain = swapchain;
		vk_extent = info.extent;
		Log::Write("Overlay: Vulkan overlay ready on {} swapchain, format {}, {}x{}, {} images",
			known ? "the game's" : "a guessed", (int)info.format, info.extent.width, info.extent.height, image_count);
		return true;
	}

	// Returns the semaphore the real present should wait on, or VK_NULL_HANDLE to present untouched.
	static VkSemaphore RenderVulkan(VkQueue queue, const VkPresentInfoKHR* present_info)
	{
		if (g_shutdown || present_info->swapchainCount == 0)
		{
			return VK_NULL_HANDLE;
		}
		if (ActiveBackend() == Backend::DX12)
		{
			RAMPAGIO_LOG_ONCE("Overlay: Vulkan presenting, but DX12 already owns the overlay");
			return VK_NULL_HANDLE;
		}
		VkDevice game_device = vk_game_device;
		if (!game_device)
		{
			RAMPAGIO_LOG_ONCE("Overlay: Vulkan present seen, waiting for vkAcquireNextImageKHR to reveal the device");
			return VK_NULL_HANDLE;
		}

		std::lock_guard lock(vk_mutex);
		if (vk_disabled)
		{
			return VK_NULL_HANDLE;
		}

		if (!vk_device)
		{
			if (!BindDevice(game_device))
			{
				vk_disabled = true;
				Log::Write("Overlay: Vulkan device setup failed");
				return VK_NULL_HANDLE;
			}
			Log::Write("Overlay: Vulkan device bound, queue family {}", vk_queue_family);
		}
		if (game_device != vk_device)
		{
			RAMPAGIO_LOG_ONCE("Overlay: the game switched Vulkan devices; overlay stays on the first one");
			return VK_NULL_HANDLE;
		}

		const VkSwapchainKHR swapchain = present_info->pSwapchains[0];
		if (swapchain != vk_swapchain)
		{
			if (!IsKnownSwapchain(swapchain) && vk_unknown_swapchain_frames < kGuessAtFrame)
			{
				const uint32_t frame = ++vk_unknown_swapchain_frames;
				if (frame == kSuboptimalAtFrame)
				{
					Log::Write("Overlay: Vulkan swapchain predates hooks, asking the game to recreate it (suboptimal)");
					vk_force_suboptimal = true;
				}
				else if (frame == kOutOfDateAtFrame)
				{
					Log::Write("Overlay: game ignored suboptimal, asking again (out of date)");
					vk_force_out_of_date = true;
				}
				return VK_NULL_HANDLE;
			}
			if (!SetupSwapchain(swapchain, queue))
			{
				DestroyFrames();
				vk_disabled = true;
				Log::Write("Overlay: Vulkan swapchain setup failed");
				return VK_NULL_HANDLE;
			}
		}

		if (!ClaimBackend(Backend::Vulkan))
		{
			RAMPAGIO_LOG_ONCE("Overlay: Vulkan presenting, but {} already owns the overlay", ActiveBackendName());
			return VK_NULL_HANDLE;
		}
		if (!ShouldRender())
		{
			return VK_NULL_HANDLE;
		}

		const uint32_t image_index = present_info->pImageIndices[0];
		if (image_index >= vk_frames.size())
		{
			return VK_NULL_HANDLE;
		}
		VkFrame& frame = vk_frames[image_index];
		vkfn::vkWaitForFences(vk_device, 1, &frame.fence, VK_TRUE, UINT64_MAX);

		std::lock_guard imgui_lock(ImGuiMutex());
		ImGui_ImplVulkan_NewFrame();
		BuildFrame();

		vkfn::vkResetCommandBuffer(frame.command_buffer, 0);
		VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkfn::vkBeginCommandBuffer(frame.command_buffer, &begin_info);

		VkRenderPassBeginInfo rp_info = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
		rp_info.renderPass = vk_render_pass;
		rp_info.framebuffer = frame.framebuffer;
		rp_info.renderArea.extent = vk_extent;
		vkfn::vkCmdBeginRenderPass(frame.command_buffer, &rp_info, VK_SUBPASS_CONTENTS_INLINE);
		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), frame.command_buffer);
		vkfn::vkCmdEndRenderPass(frame.command_buffer);
		vkfn::vkEndCommandBuffer(frame.command_buffer);

		std::vector<VkPipelineStageFlags> wait_stages(present_info->waitSemaphoreCount, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
		VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
		submit.waitSemaphoreCount = present_info->waitSemaphoreCount;
		submit.pWaitSemaphores = present_info->pWaitSemaphores;
		submit.pWaitDstStageMask = wait_stages.data();
		submit.commandBufferCount = 1;
		submit.pCommandBuffers = &frame.command_buffer;
		submit.signalSemaphoreCount = 1;
		submit.pSignalSemaphores = &frame.render_done;

		vkfn::vkResetFences(vk_device, 1, &frame.fence);
		if (vkfn::vkQueueSubmit(queue, 1, &submit, frame.fence) != VK_SUCCESS)
		{
			// Nothing was consumed; present with the game's own semaphores. The fence is now unsignaled
			// and would hang the next wait, so stop rendering.
			vk_disabled = true;
			Log::Write("Overlay: Vulkan submit failed, overlay disabled");
			return VK_NULL_HANDLE;
		}
		RAMPAGIO_LOG_ONCE("Overlay: Vulkan frame submitted");
		return frame.render_done;
	}

	static VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR_hook(VkQueue queue, const VkPresentInfoKHR* present_info)
	{
		InHook guard;
		g_vulkan_present_seen = true;
		RAMPAGIO_LOG_ONCE("Overlay: Vulkan present hook is live");
		VkResult result;
		VkSemaphore semaphore = present_info ? RenderVulkan(queue, present_info) : VK_NULL_HANDLE;
		if (semaphore)
		{
			VkPresentInfoKHR patched = *present_info;
			patched.waitSemaphoreCount = 1;
			patched.pWaitSemaphores = &semaphore;
			result = original_vkQueuePresentKHR(queue, &patched);
		}
		else
		{
			result = original_vkQueuePresentKHR(queue, present_info);
		}
		// The frame was presented normally; this only tells the game its swapchain could be better.
		if (result == VK_SUCCESS && vk_force_suboptimal.exchange(false))
		{
			return VK_SUBOPTIMAL_KHR;
		}
		return result;
	}

	// Returning OUT_OF_DATE without calling the driver leaves the semaphore and fence untouched,
	// exactly as the spec says for that error, so the game just recreates the swapchain.
	static VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImageKHR_hook(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* image_index)
	{
		InHook guard;
		vk_game_device = device;
		RAMPAGIO_LOG_ONCE("Overlay: Vulkan acquire hook is live");
		if (vk_force_out_of_date.exchange(false))
		{
			return VK_ERROR_OUT_OF_DATE_KHR;
		}
		return original_vkAcquireNextImageKHR(device, swapchain, timeout, semaphore, fence, image_index);
	}

	static VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImage2KHR_hook(VkDevice device, const VkAcquireNextImageInfoKHR* acquire_info, uint32_t* image_index)
	{
		InHook guard;
		vk_game_device = device;
		RAMPAGIO_LOG_ONCE("Overlay: Vulkan acquire2 hook is live");
		if (vk_force_out_of_date.exchange(false))
		{
			return VK_ERROR_OUT_OF_DATE_KHR;
		}
		return original_vkAcquireNextImage2KHR(device, acquire_info, image_index);
	}

	static VKAPI_ATTR VkResult VKAPI_CALL vkCreateSwapchainKHR_hook(VkDevice device, const VkSwapchainCreateInfoKHR* create_info, const VkAllocationCallbacks* allocator, VkSwapchainKHR* swapchain)
	{
		InHook guard;
		VkResult result = original_vkCreateSwapchainKHR(device, create_info, allocator, swapchain);
		if (result == VK_SUCCESS && create_info)
		{
			std::lock_guard lock(vk_info_mutex);
			vk_swapchain_infos[*swapchain] = { create_info->imageFormat, create_info->imageExtent, create_info->minImageCount };
			Log::Write("Overlay: Vulkan swapchain created, format {}, {}x{}, min {} images",
				(int)create_info->imageFormat, create_info->imageExtent.width, create_info->imageExtent.height, create_info->minImageCount);
		}
		return result;
	}

	static VKAPI_ATTR void VKAPI_CALL vkDestroySwapchainKHR_hook(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* allocator)
	{
		InHook guard;
		{
			std::lock_guard lock(vk_mutex);
			if (swapchain != VK_NULL_HANDLE && swapchain == vk_swapchain)
			{
				DestroyFrames();
			}
		}
		{
			std::lock_guard lock(vk_info_mutex);
			vk_swapchain_infos.erase(swapchain);
		}
		original_vkDestroySwapchainKHR(device, swapchain, allocator);
	}

	static VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice_hook(VkPhysicalDevice physical_device, const VkDeviceCreateInfo* create_info, const VkAllocationCallbacks* allocator, VkDevice* device)
	{
		InHook guard;
		VkResult result = original_vkCreateDevice(physical_device, create_info, allocator, device);
		if (result == VK_SUCCESS && create_info && vkfn::vkGetPhysicalDeviceQueueFamilyProperties)
		{
			uint32_t count = 0;
			vkfn::vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, nullptr);
			std::vector<VkQueueFamilyProperties> families(count);
			vkfn::vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, families.data());

			VkDeviceInfo info = { physical_device, UINT32_MAX };
			for (uint32_t i = 0; i < create_info->queueCreateInfoCount; i++)
			{
				uint32_t family = create_info->pQueueCreateInfos[i].queueFamilyIndex;
				if (family < count && (families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT))
				{
					info.graphics_family = family;
					break;
				}
			}
			if (info.graphics_family != UINT32_MAX)
			{
				std::lock_guard lock(vk_info_mutex);
				vk_device_infos[*device] = info;
			}
		}
		return result;
	}

	static bool HasExtension(const std::vector<VkExtensionProperties>& extensions, const char* name)
	{
		for (const auto& ext : extensions)
		{
			if (strcmp(ext.extensionName, name) == 0)
			{
				return true;
			}
		}
		return false;
	}

	static bool Hook(void* target, void* detour, void** original)
	{
		if (!target)
		{
			return false;
		}
		if (MH_CreateHook(target, detour, original) != MH_OK || MH_QueueEnableHook(target) != MH_OK)
		{
			return false;
		}
		TrackHook(target);
		return true;
	}

	bool InstallVulkanHooks()
	{
		HMODULE loader = GetModuleHandleA("vulkan-1.dll");
		vkfn::vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader, "vkGetInstanceProcAddr"));
		if (!vkfn::vkGetInstanceProcAddr)
		{
			return false;
		}
		auto vkCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(vkfn::vkGetInstanceProcAddr(nullptr, "vkCreateInstance"));
		auto vkEnumerateInstanceExtensionProperties = reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(vkfn::vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceExtensionProperties"));
		if (!vkCreateInstance || !vkEnumerateInstanceExtensionProperties)
		{
			return false;
		}

		// ImGui's loader wants the surface functions to resolve, so enable the surface extensions if present.
		uint32_t ext_count = 0;
		vkEnumerateInstanceExtensionProperties(nullptr, &ext_count, nullptr);
		std::vector<VkExtensionProperties> instance_exts(ext_count);
		vkEnumerateInstanceExtensionProperties(nullptr, &ext_count, instance_exts.data());
		std::vector<const char*> enabled_instance_exts;
		for (const char* name : { "VK_KHR_surface", "VK_KHR_win32_surface" })
		{
			if (HasExtension(instance_exts, name))
			{
				enabled_instance_exts.push_back(name);
			}
		}

		VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
		app.pApplicationName = "Rampagio";
		app.apiVersion = VK_API_VERSION_1_0;
		VkInstanceCreateInfo instance_info = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
		instance_info.pApplicationInfo = &app;
		instance_info.enabledExtensionCount = (uint32_t)enabled_instance_exts.size();
		instance_info.ppEnabledExtensionNames = enabled_instance_exts.data();
		if (vkCreateInstance(&instance_info, nullptr, &vk_instance) != VK_SUCCESS)
		{
			return false;
		}

#define RAMPAGIO_VK_INSTANCE_FN(name) reinterpret_cast<PFN_##name>(vkfn::vkGetInstanceProcAddr(vk_instance, #name))
		auto vkEnumeratePhysicalDevices = RAMPAGIO_VK_INSTANCE_FN(vkEnumeratePhysicalDevices);
		auto vkGetPhysicalDeviceProperties = RAMPAGIO_VK_INSTANCE_FN(vkGetPhysicalDeviceProperties);
		auto vkEnumerateDeviceExtensionProperties = RAMPAGIO_VK_INSTANCE_FN(vkEnumerateDeviceExtensionProperties);
		auto vkCreateDevice = RAMPAGIO_VK_INSTANCE_FN(vkCreateDevice);
		auto vkDestroyDevice = RAMPAGIO_VK_INSTANCE_FN(vkDestroyDevice);
		vkfn::vkGetDeviceProcAddr = RAMPAGIO_VK_INSTANCE_FN(vkGetDeviceProcAddr);
		vkfn::vkGetPhysicalDeviceQueueFamilyProperties = RAMPAGIO_VK_INSTANCE_FN(vkGetPhysicalDeviceQueueFamilyProperties);
#undef RAMPAGIO_VK_INSTANCE_FN

		// Prefer the discrete GPU; that's the one the game renders on in a hybrid setup.
		uint32_t gpu_count = 0;
		vkEnumeratePhysicalDevices(vk_instance, &gpu_count, nullptr);
		std::vector<VkPhysicalDevice> gpus(gpu_count);
		vkEnumeratePhysicalDevices(vk_instance, &gpu_count, gpus.data());
		for (VkPhysicalDevice gpu : gpus)
		{
			VkPhysicalDeviceProperties props;
			vkGetPhysicalDeviceProperties(gpu, &props);
			if (!vk_hooked_physical || props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
			{
				vk_hooked_physical = gpu;
				if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
				{
					break;
				}
			}
		}
		if (!vk_hooked_physical)
		{
			return false;
		}

		uint32_t dev_ext_count = 0;
		vkEnumerateDeviceExtensionProperties(vk_hooked_physical, nullptr, &dev_ext_count, nullptr);
		std::vector<VkExtensionProperties> device_exts(dev_ext_count);
		vkEnumerateDeviceExtensionProperties(vk_hooked_physical, nullptr, &dev_ext_count, device_exts.data());
		if (!HasExtension(device_exts, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
		{
			return false;
		}

		const float priority = 1.0f;
		VkDeviceQueueCreateInfo queue_info = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
		queue_info.queueFamilyIndex = FindGraphicsFamily(vk_hooked_physical);
		queue_info.queueCount = 1;
		queue_info.pQueuePriorities = &priority;
		const char* device_ext_names[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
		VkDeviceCreateInfo device_info = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
		device_info.queueCreateInfoCount = 1;
		device_info.pQueueCreateInfos = &queue_info;
		device_info.enabledExtensionCount = 1;
		device_info.ppEnabledExtensionNames = device_ext_names;
		VkDevice dummy = VK_NULL_HANDLE;
		if (vkCreateDevice(vk_hooked_physical, &device_info, nullptr, &dummy) != VK_SUCCESS)
		{
			return false;
		}

		void* present = reinterpret_cast<void*>(vkfn::vkGetDeviceProcAddr(dummy, "vkQueuePresentKHR"));
		void* acquire = reinterpret_cast<void*>(vkfn::vkGetDeviceProcAddr(dummy, "vkAcquireNextImageKHR"));
		void* acquire2 = reinterpret_cast<void*>(vkfn::vkGetDeviceProcAddr(dummy, "vkAcquireNextImage2KHR"));
		void* create_swapchain = reinterpret_cast<void*>(vkfn::vkGetDeviceProcAddr(dummy, "vkCreateSwapchainKHR"));
		void* destroy_swapchain = reinterpret_cast<void*>(vkfn::vkGetDeviceProcAddr(dummy, "vkDestroySwapchainKHR"));
		vkDestroyDevice(dummy, nullptr);

		bool ok = Hook(present, reinterpret_cast<void*>(vkQueuePresentKHR_hook), reinterpret_cast<void**>(&original_vkQueuePresentKHR)) &&
			Hook(acquire, reinterpret_cast<void*>(vkAcquireNextImageKHR_hook), reinterpret_cast<void**>(&original_vkAcquireNextImageKHR)) &&
			Hook(create_swapchain, reinterpret_cast<void*>(vkCreateSwapchainKHR_hook), reinterpret_cast<void**>(&original_vkCreateSwapchainKHR)) &&
			Hook(destroy_swapchain, reinterpret_cast<void*>(vkDestroySwapchainKHR_hook), reinterpret_cast<void**>(&original_vkDestroySwapchainKHR));
		if (!ok)
		{
			for (void* target : { present, acquire, create_swapchain, destroy_swapchain })
			{
				if (target)
				{
					MH_RemoveHook(target);
					UntrackHook(target);
				}
			}
			return false;
		}
		// Optional: Vulkan 1.1 acquire path, and vkCreateDevice to learn the game's GPU and queue family
		// when we are loaded early enough.
		Hook(acquire2, reinterpret_cast<void*>(vkAcquireNextImage2KHR_hook), reinterpret_cast<void**>(&original_vkAcquireNextImage2KHR));
		Hook(reinterpret_cast<void*>(vkCreateDevice), reinterpret_cast<void*>(vkCreateDevice_hook), reinterpret_cast<void**>(&original_vkCreateDevice));
		MH_ApplyQueued();
		Log::Write("Overlay: Vulkan hooks installed");
		return true;
	}
}

namespace Overlay
{
	void ShutdownVulkan()
	{
		std::lock_guard lock(vk_mutex);
		if (vk_device)
		{
			DestroyFrames(); // waits on each frame's fence
			if (vk_backend_ready)
			{
				std::lock_guard imgui_lock(ImGuiMutex());
				ImGui_ImplVulkan_Shutdown();
				vk_backend_ready = false;
			}
			if (vk_render_pass)
			{
				vkfn::vkDestroyRenderPass(vk_device, vk_render_pass, nullptr);
				vk_render_pass = VK_NULL_HANDLE;
			}
			if (vk_command_pool)
			{
				auto destroy_pool = reinterpret_cast<PFN_vkDestroyCommandPool>(vkfn::vkGetDeviceProcAddr(vk_device, "vkDestroyCommandPool"));
				if (destroy_pool)
				{
					destroy_pool(vk_device, vk_command_pool, nullptr);
				}
				vk_command_pool = VK_NULL_HANDLE;
			}
			vk_device = VK_NULL_HANDLE; // the game's; not ours to destroy
		}
		if (vk_instance)
		{
			auto destroy_instance = reinterpret_cast<PFN_vkDestroyInstance>(vkfn::vkGetInstanceProcAddr(vk_instance, "vkDestroyInstance"));
			if (destroy_instance)
			{
				destroy_instance(vk_instance, nullptr);
			}
			vk_instance = VK_NULL_HANDLE;
		}
	}
}

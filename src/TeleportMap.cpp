#include "TeleportMap.h"
#include "GameUtil.h"
#include "Log.h"
#include "MainThread.h"
#include "Resource.h"
#include "menus\Menus.h"
#include "overlay\Overlay.h"

#include "imgui.h"
#include "imgui_internal.h" // RegisterUserTexture

#include <windows.h>
#include <wincodec.h>
#include <wrl\client.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <format>
#include <mutex>
#include <string>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")

namespace
{
	// World x/y -> picture u/v (0..1): u = kU[0] x + kU[1] y + kU[2], v the
	// same with kV. Least-squares fit of seven train stations (the map's
	// station dots against the post offices' coordinates in Teleports.inc:
	// Valentine, Saint Denis, Rhodes, Annesburg, Emerald Ranch, Benedict
	// Point, Wallace Station), measured on the 9000 x 7004 original; about
	// 13 m RMS, 9-57 m when each station is left out of the fit in turn.
	constexpr double kU[3] = { 8.78184598e-05, 7.85527754e-07, 0.632043151 };
	constexpr double kV[3] = { 9.30128526e-07, -0.000114936342, 0.398940415 };

	ImVec2 ToUv(float x, float y)
	{
		return { static_cast<float>(kU[0] * x + kU[1] * y + kU[2]), static_cast<float>(kV[0] * x + kV[1] * y + kV[2]) };
	}

	void FromUv(ImVec2 uv, float& x, float& y)
	{
		const double det = kU[0] * kV[1] - kU[1] * kV[0];
		const double du = uv.x - kU[2], dv = uv.y - kV[2];
		x = static_cast<float>((du * kV[1] - dv * kU[1]) / det);
		y = static_cast<float>((dv * kU[0] - du * kV[0]) / det);
	}

	// --- the picture ---------------------------------------------------------------------

	// Decoded on the script thread (once, on first use), handed to the render
	// thread, which makes it an ImGui user texture; the renderer backends
	// upload it (and re-upload it if they restart).
	std::mutex g_pixelsMutex;
	std::vector<unsigned char> g_pixels;
	int g_width = 0, g_height = 0;
	std::atomic<bool> g_decoded = false;
	bool g_decodeTried = false; // script thread

	ImTextureData g_texture; // render thread
	bool g_registered = false;

	bool Decode()
	{
		HMODULE self = nullptr;
		GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(&Decode), &self);
		HRSRC resource = FindResourceW(self, MAKEINTRESOURCEW(IDR_TELEPORT_MAP), MAKEINTRESOURCEW(10)); // RT_RCDATA (this project is MBCS)
		HGLOBAL loaded = resource ? LoadResource(self, resource) : nullptr;
		const void* bytes = loaded ? LockResource(loaded) : nullptr;
		const DWORD size = resource ? SizeofResource(self, resource) : 0;
		if (!bytes || !size)
		{
			Log::Write("[TeleportMap] The map picture isn't in the .asi");
			return false;
		}

		const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		bool ok = false;
		{
			using Microsoft::WRL::ComPtr;
			ComPtr<IWICImagingFactory> factory;
			ComPtr<IWICStream> stream;
			ComPtr<IWICBitmapDecoder> decoder;
			ComPtr<IWICBitmapFrameDecode> frame;
			ComPtr<IWICBitmapSource> rgba;
			UINT w = 0, h = 0;
			if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))
				&& SUCCEEDED(factory->CreateStream(&stream))
				&& SUCCEEDED(stream->InitializeFromMemory(static_cast<BYTE*>(const_cast<void*>(bytes)), size))
				&& SUCCEEDED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder))
				&& SUCCEEDED(decoder->GetFrame(0, &frame))
				&& SUCCEEDED(WICConvertBitmapSource(GUID_WICPixelFormat32bppRGBA, frame.Get(), &rgba))
				&& SUCCEEDED(rgba->GetSize(&w, &h)) && w && h)
			{
				std::vector<unsigned char> pixels(static_cast<size_t>(w) * h * 4);
				if (SUCCEEDED(rgba->CopyPixels(nullptr, w * 4, static_cast<UINT>(pixels.size()), pixels.data())))
				{
					std::lock_guard lock(g_pixelsMutex);
					g_pixels = std::move(pixels);
					g_width = static_cast<int>(w);
					g_height = static_cast<int>(h);
					ok = true;
				}
			}
		}
		if (SUCCEEDED(init))
			CoUninitialize();
		if (!ok)
			Log::Write("[TeleportMap] Couldn't decode the map picture");
		return ok;
	}

	// Render thread: the texture, once decoded. False while it isn't there.
	bool EnsureTexture()
	{
		if (g_registered)
			return true;
		if (!g_decoded)
			return false;
		std::lock_guard lock(g_pixelsMutex);
		g_texture.Create(ImTextureFormat_RGBA32, g_width, g_height);
		std::memcpy(g_texture.Pixels, g_pixels.data(), g_pixels.size());
		g_texture.UseColors = true;
		ImGui::RegisterUserTexture(&g_texture);
		g_registered = true;
		std::vector<unsigned char>().swap(g_pixels); // ImGui keeps its own copy
		return true;
	}

	// --- what the overlay draws (script thread writes, render thread reads) --------------

	struct Snapshot
	{
		bool player = false;
		float playerX = 0, playerY = 0, heading = 0;
		bool horse = false;
		float horseX = 0, horseY = 0;
		bool waypoint = false;
		float waypointX = 0, waypointY = 0;
		bool side = false;
		float sideX = 0, sideY = 0;
		std::string sideCaption;
		float menuLeft = 0, menuTop = 0;
	};
	std::mutex g_snapshotMutex;
	Snapshot g_snapshot;

	Snapshot Read()
	{
		std::lock_guard lock(g_snapshotMutex);
		return g_snapshot;
	}

	// ShowBeside's request for this frame (script thread).
	bool g_sideRequested = false;
	float g_sideX = 0, g_sideY = 0;
	std::string g_sideCaption;

	// --- drawing (render thread) ---------------------------------------------------------

	const ImU32 kBackground = IM_COL32(214, 194, 146, 255); // the picture's paper
	const ImU32 kPlace = IM_COL32(150, 30, 20, 220);
	const ImU32 kPlaceHot = IM_COL32(230, 40, 20, 255);
	const ImU32 kPlayer = IM_COL32(40, 110, 230, 255);
	const ImU32 kHorse = IM_COL32(120, 70, 30, 255);
	const ImU32 kWaypoint = IM_COL32(170, 60, 200, 255);
	const ImU32 kTarget = IM_COL32(220, 20, 20, 255);
	const ImU32 kOutline = IM_COL32(0, 0, 0, 200);

	// Picture u/v <-> screen, for a view centred on `center` with `zoom`
	// screen pixels per picture pixel.
	struct View
	{
		ImVec2 origin; // the canvas centre on screen
		ImVec2 center; // u/v at the origin
		float zoom;

		ImVec2 ToScreen(ImVec2 uv) const
		{
			return { origin.x + (uv.x - center.x) * g_texture.Width * zoom, origin.y + (uv.y - center.y) * g_texture.Height * zoom };
		}
		ImVec2 ToUv(ImVec2 screen) const
		{
			return { center.x + (screen.x - origin.x) / (g_texture.Width * zoom), center.y + (screen.y - origin.y) / (g_texture.Height * zoom) };
		}
		ImVec2 World(float x, float y) const { return ToScreen(::ToUv(x, y)); }
	};

	void DrawPicture(ImDrawList* draw, const View& view, ImVec2 p0, ImVec2 p1)
	{
		draw->AddRectFilled(p0, p1, kBackground);
		draw->AddImage(g_texture.GetTexRef(), view.ToScreen({ 0, 0 }), view.ToScreen({ 1, 1 }));
	}

	// An arrow pointing where the player faces. RDR2 headings: 0 is north
	// (+y), 90 west.
	void DrawPlayer(ImDrawList* draw, const View& view, const Snapshot& s, float size)
	{
		const float rad = s.heading * 3.14159265f / 180.0f;
		const ImVec2 at = view.World(s.playerX, s.playerY);
		const ImVec2 ahead = view.World(s.playerX - std::sin(rad) * 100.0f, s.playerY + std::cos(rad) * 100.0f);
		ImVec2 dir = { ahead.x - at.x, ahead.y - at.y };
		const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
		dir = len > 0 ? ImVec2(dir.x / len, dir.y / len) : ImVec2(0, -1);
		const ImVec2 side = { -dir.y, dir.x };
		const ImVec2 tip = { at.x + dir.x * size, at.y + dir.y * size };
		const ImVec2 left = { at.x - dir.x * size * 0.6f + side.x * size * 0.6f, at.y - dir.y * size * 0.6f + side.y * size * 0.6f };
		const ImVec2 right = { at.x - dir.x * size * 0.6f - side.x * size * 0.6f, at.y - dir.y * size * 0.6f - side.y * size * 0.6f };
		draw->AddTriangleFilled(tip, left, right, kPlayer);
		draw->AddTriangle(tip, left, right, kOutline, 1.5f);
	}

	void DrawMarkers(ImDrawList* draw, const View& view, const Snapshot& s, float size)
	{
		if (s.waypoint)
		{
			const ImVec2 w = view.World(s.waypointX, s.waypointY);
			draw->AddQuadFilled({ w.x, w.y - size }, { w.x + size, w.y }, { w.x, w.y + size }, { w.x - size, w.y }, kWaypoint);
			draw->AddQuad({ w.x, w.y - size }, { w.x + size, w.y }, { w.x, w.y + size }, { w.x - size, w.y }, kOutline, 1.5f);
		}
		if (s.horse)
		{
			const ImVec2 h = view.World(s.horseX, s.horseY);
			draw->AddCircleFilled(h, size * 0.6f, kHorse);
			draw->AddCircle(h, size * 0.6f, kOutline, 0, 1.5f);
		}
		if (s.player)
			DrawPlayer(draw, view, s, size);
	}

	// --- Teleport > Map (interactive) ----------------------------------------------------

	Overlay::Tool g_window = { "Teleport Map", nullptr };

	struct WindowState
	{
		ImVec2 center = { 0.5f, 0.5f };
		float zoom = 0; // 0: fit the whole picture on the next frame
		bool places = true;
		bool follow = false;
		bool dragged = false;
		float menuX = 0, menuY = 0;
		int menuPlace = -1;
	};
	WindowState g_view;

	void DrawWindow(bool* open)
	{
		const Snapshot s = Read();
		const float em = ImGui::GetFontSize();
		ImGui::SetNextWindowSize(ImVec2(em * 62, em * 46), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Teleport Map###RampagioMap", open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
		{
			ImGui::End();
			return;
		}
		ImGui::Checkbox("Places", &g_view.places);
		ImGui::SameLine();
		ImGui::Checkbox("Follow Player", &g_view.follow);
		ImGui::SameLine();
		const bool centerPlayer = ImGui::Button("Center on Player");
		ImGui::SameLine();
		const bool whole = ImGui::Button("Whole Map");
		ImGui::SameLine();
		ImGui::TextDisabled("Drag to pan, wheel to zoom, click a place or right-click to teleport.");

		if (!EnsureTexture())
		{
			ImGui::TextUnformatted("Loading the map...");
			ImGui::End();
			return;
		}

		const ImVec2 avail = ImGui::GetContentRegionAvail();
		if (avail.x < 50 || avail.y < 50)
		{
			ImGui::End();
			return;
		}
		ImGui::InvisibleButton("canvas", avail, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
		const ImVec2 p0 = ImGui::GetItemRectMin(), p1 = ImGui::GetItemRectMax();
		const bool hovered = ImGui::IsItemHovered();
		const float fit = (std::min)(avail.x / g_texture.Width, avail.y / g_texture.Height);
		if (g_view.zoom <= 0 || whole)
		{
			g_view.zoom = fit;
			g_view.center = { 0.5f, 0.5f };
			g_view.follow = false;
		}
		View view{ { (p0.x + p1.x) / 2, (p0.y + p1.y) / 2 }, g_view.center, g_view.zoom };
		const ImGuiIO& io = ImGui::GetIO();

		if (hovered && io.MouseWheel != 0)
		{
			const ImVec2 before = view.ToUv(io.MousePos);
			g_view.zoom = std::clamp(g_view.zoom * std::pow(1.25f, io.MouseWheel), fit * 0.5f, 6.0f);
			view.zoom = g_view.zoom;
			const ImVec2 after = view.ToUv(io.MousePos);
			if (!g_view.follow)
				g_view.center = { g_view.center.x + before.x - after.x, g_view.center.y + before.y - after.y };
		}
		if (ImGui::IsItemActivated())
			g_view.dragged = false;
		if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f))
		{
			const ImVec2 delta = io.MouseDelta;
			g_view.center = { g_view.center.x - delta.x / (g_texture.Width * g_view.zoom), g_view.center.y - delta.y / (g_texture.Height * g_view.zoom) };
			g_view.dragged = true;
			g_view.follow = false;
		}
		if ((g_view.follow || centerPlayer) && s.player)
			g_view.center = ToUv(s.playerX, s.playerY);
		view.center = g_view.center;

		ImDrawList* draw = ImGui::GetWindowDrawList();
		draw->PushClipRect(p0, p1, true);
		DrawPicture(draw, view, p0, p1);

		// Places, and the one under the cursor.
		const auto places = Menus::TeleportPlaces();
		int hot = -1;
		float hotDistance = 8.0f * 8.0f;
		if (g_view.places)
			for (int i = 0; i < static_cast<int>(places.size()); ++i)
			{
				const ImVec2 at = view.World(places[i].x, places[i].y);
				if (at.x < p0.x || at.y < p0.y || at.x > p1.x || at.y > p1.y)
					continue;
				draw->AddCircleFilled(at, 3.5f, kPlace);
				const float dx = at.x - io.MousePos.x, dy = at.y - io.MousePos.y;
				if (hovered && dx * dx + dy * dy < hotDistance)
				{
					hotDistance = dx * dx + dy * dy;
					hot = i;
				}
			}
		if (hot >= 0)
			draw->AddCircle(view.World(places[hot].x, places[hot].y), 6.0f, kPlaceHot, 0, 2.0f);
		DrawMarkers(draw, view, s, em * 0.6f);

		// The world position under the cursor.
		if (hovered)
		{
			float x, y;
			FromUv(view.ToUv(io.MousePos), x, y);
			const std::string text = std::format("{:.0f}, {:.0f}", x, y);
			draw->AddRectFilled({ p0.x, p1.y - em * 1.4f }, { p0.x + em * 0.6f * text.size() + em, p1.y }, IM_COL32(0, 0, 0, 150));
			draw->AddText({ p0.x + em * 0.5f, p1.y - em * 1.2f }, IM_COL32_WHITE, text.c_str());
		}
		draw->PopClipRect();

		if (hot >= 0)
			ImGui::SetTooltip("%s\n%s", places[hot].name, places[hot].menu);

		// Left click (not a drag) on a place: go there.
		if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !g_view.dragged && hot >= 0)
		{
			const Menus::TeleportPlace place = places[hot];
			MainThread::Post([place] { Menus::TeleportToPlace(place.x, place.y, place.z); });
		}
		// Right click: the context menu at that spot.
		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
		{
			FromUv(view.ToUv(io.MousePos), g_view.menuX, g_view.menuY);
			g_view.menuPlace = hot;
			ImGui::OpenPopup("map context");
		}
		if (ImGui::BeginPopup("map context"))
		{
			ImGui::TextDisabled("%.0f, %.0f", g_view.menuX, g_view.menuY);
			if (ImGui::MenuItem("Teleport Here"))
			{
				const float x = g_view.menuX, y = g_view.menuY;
				MainThread::Post([x, y]
				{
					const std::string error = Menus::TeleportToGround(x, y);
					if (!error.empty())
						Log::Write("[TeleportMap] {}", error);
				});
			}
			if (g_view.menuPlace >= 0 && g_view.menuPlace < static_cast<int>(places.size()))
			{
				const Menus::TeleportPlace place = places[g_view.menuPlace];
				if (ImGui::MenuItem(std::format("Teleport to {}", place.name).c_str()))
					MainThread::Post([place] { Menus::TeleportToPlace(place.x, place.y, place.z); });
			}
			ImGui::EndPopup();
		}
		ImGui::End();
	}

	// --- beside the menu (passive) -------------------------------------------------------

	Overlay::Tool g_side = { "Teleport Map (menu)", nullptr };

	// How much of the world the side map spans, in metres.
	constexpr float kSideSpan = 3000.0f;

	void DrawSide(bool*)
	{
		const Snapshot s = Read();
		if (!s.side || !EnsureTexture())
			return;
		const ImGuiIO& io = ImGui::GetIO();
		const float W = io.DisplaySize.x, H = io.DisplaySize.y;
		const float size = 0.34f * H;
		const float em = ImGui::GetFontSize();
		// Where Spawner Previews put their picture (Previews.cpp): right of
		// the menu, or left of it when the menu sits right of 0.57.
		const float x = s.menuLeft <= 0.57f ? (s.menuLeft + 0.245f) * W : (s.menuLeft - 0.005f) * W - size;
		const float y = (s.menuTop + 0.025f) * H;
		ImGui::SetNextWindowPos({ x, y });
		ImGui::SetNextWindowSize({ size, size + em * 1.6f });
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0, 0 });
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::Begin("##RampagioSideMap", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings
			| ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBackground);
		const ImVec2 p0 = { x, y }, p1 = { x + size, y + size };
		// Picture pixels per metre, from the calibration's x scale.
		const float pixelsPerMetre = static_cast<float>(kU[0]) * g_texture.Width;
		View view{ { (p0.x + p1.x) / 2, (p0.y + p1.y) / 2 }, ToUv(s.sideX, s.sideY), size / (kSideSpan * pixelsPerMetre) };
		ImDrawList* draw = ImGui::GetWindowDrawList();
		draw->PushClipRect(p0, p1, true);
		DrawPicture(draw, view, p0, p1);
		DrawMarkers(draw, view, s, em * 0.55f);
		const ImVec2 target = view.World(s.sideX, s.sideY);
		draw->AddCircle(target, em * 0.7f, kTarget, 0, 3.0f);
		draw->AddLine({ target.x - em, target.y }, { target.x + em, target.y }, kTarget, 2.0f);
		draw->AddLine({ target.x, target.y - em }, { target.x, target.y + em }, kTarget, 2.0f);
		draw->PopClipRect();
		draw->AddRect(p0, p1, IM_COL32(0, 0, 0, 255), 0, 0, 2.0f);
		draw->AddRectFilled({ p0.x, p1.y }, { p1.x, p1.y + em * 1.6f }, IM_COL32(0, 0, 0, 200));
		const ImVec2 textSize = ImGui::CalcTextSize(s.sideCaption.c_str());
		draw->AddText({ p0.x + (size - textSize.x) / 2, p1.y + em * 0.3f }, IM_COL32_WHITE, s.sideCaption.c_str());
		ImGui::End();
		ImGui::PopStyleVar(2);
	}
}

namespace TeleportMap
{
	void Register()
	{
		g_window.draw = DrawWindow;
		Overlay::Register(g_window);
		g_side.draw = DrawSide;
		g_side.passive = true;
		Overlay::Register(g_side);
	}

	void OpenWindow()
	{
		Overlay::SetOpen(g_window, true);
	}

	void ShowBeside(float x, float y, std::string_view caption)
	{
		g_sideRequested = true;
		g_sideX = x;
		g_sideY = y;
		if (caption != g_sideCaption)
			g_sideCaption.assign(caption);
	}

	void Tick()
	{
		const bool side = g_sideRequested;
		g_sideRequested = false;
		if ((side || g_window.open) && !g_decodeTried)
		{
			g_decodeTried = true;
			const ULONGLONG start = GetTickCount64();
			if (Decode())
			{
				g_decoded = true;
				Log::Write("[TeleportMap] Decoded the map picture ({} x {}) in {} ms", g_width, g_height, GetTickCount64() - start);
			}
		}
		if (side != g_side.open)
			Overlay::SetOpen(g_side, side);
		if (!side && !g_window.open)
			return;

		Snapshot s;
		const Ped ped = PLAYER::PLAYER_PED_ID();
		if (ENTITY::DOES_ENTITY_EXIST(ped))
		{
			const Vector3 at = ENTITY::GET_ENTITY_COORDS(ped, TRUE, TRUE);
			s.player = true;
			s.playerX = at.x;
			s.playerY = at.y;
			s.heading = ENTITY::GET_ENTITY_HEADING(ped);
		}
		// The horse when it isn't under the player.
		if (const Ped horse = GameUtil::PlayerHorse(); horse && horse != GameUtil::PlayerMount())
		{
			const Vector3 at = ENTITY::GET_ENTITY_COORDS(horse, TRUE, TRUE);
			s.horse = true;
			s.horseX = at.x;
			s.horseY = at.y;
		}
		if (MAP::IS_WAYPOINT_ACTIVE())
		{
			const Vector3 at = MAP::_GET_WAYPOINT_COORDS();
			s.waypoint = true;
			s.waypointX = at.x;
			s.waypointY = at.y;
		}
		s.side = side;
		s.sideX = g_sideX;
		s.sideY = g_sideY;
		s.sideCaption = g_sideCaption;
		s.menuLeft = Style().left;
		s.menuTop = Style().top;
		std::lock_guard lock(g_snapshotMutex);
		g_snapshot = std::move(s);
	}

	void Suspend()
	{
		Overlay::SetOpen(g_window, false);
		Overlay::SetOpen(g_side, false);
	}
}

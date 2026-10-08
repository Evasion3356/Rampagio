/*
	Miscellaneous: ports Rampage's Submenus::SubMiscellaneous and its
	submenus SubGameMusic, SubMusicPlayer, SubMobileTheater,
	SubCutscenePlayer, SubMinigames and SubEditVolume (the Volume Editor).

	Lists come from the game scripts (tools/extract_misc.py): music events,
	mission cutscenes, guard zones, relationship groups. The theater
	playlists are the ten the show scripts use.

	Rows marked "ours" differ from Rampage on purpose:
	- Music Player plays files from a RampagioMusic folder next to the .asi
	  through Windows MCI; Rampage bundles FMOD.
	- Volume Editor lists the volumes made here; Rampage edits existing
	  game volumes.
	- Undead Nightmare II is our own wave mode on the same models, outfits
	  and bosses Rampage uses.
	- Air Walk holds a fixed height instead of moving the player itself.
	Not ported: Friendlist (online), Social Club photo upload stats, the
	Cutscene Player's "Try to Populate" (Rampage's model table), and the
	Dev rows that open tabled areas (Global Editor, Script Tools).
	The Dev section also holds the Stat Editor (Rampage's SubStatEditor).
*/

#include "Menus.h"
#include "..\GameUtil.h"
#include "..\GamePointers.h"
#include "..\NativeHooks.h"
#include "..\keyboard.h"
#include "..\Log.h"
#include "..\LogFallback.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

namespace
{
	Ped Me() { return PLAYER::PLAYER_PED_ID(); }

	const char* const kMusicEvents[] = {
#include "..\data\MusicEvents.inc"
	};
	const char* const kCutscenes[] = {
#include "..\data\Cutscenes.inc"
	};
	const char* const kGuardZones[] = {
#include "..\data\GuardZones.inc"
	};
	const char* const kRelGroups[] = {
#include "..\data\RelGroups.inc"
	};

	constexpr Hash INPUT_SPRINT = 0x8FFC75D6;
	constexpr Hash INPUT_LOOK_LR = 0xA987235F;
	constexpr Hash INPUT_LOOK_UD = 0xD2047988;
	constexpr Hash INPUT_MOVE_LR = 0x4D8FB4C1;
	constexpr Hash INPUT_MOVE_UD = 0xFDA83190;
	constexpr Hash INPUT_MOVE_UP_ONLY = 0x8FD015D8;
	constexpr Hash INPUT_MOVE_DOWN_ONLY = 0xD27782E3;
	constexpr Hash INPUT_ATTACK = 0x6FED71BC; // group 2, as Rampage's Air Walk reads it
	constexpr Hash INPUT_AIM = 0x51104035;

	constexpr float kDegToRad = 3.14159265f / 180.0f;

	Vector3 CamForward(const Vector3& rot)
	{
		const float pitch = rot.x * kDegToRad, yaw = rot.z * kDegToRad;
		const float c = std::cos(pitch);
		return { -std::sin(yaw) * c, std::cos(yaw) * c, std::sin(pitch) };
	}

	// --- cameras -----------------------------------------------------------------

	float g_camZoom = 10.0f;

	void CamZoomTick()
	{
		CAMERA::SET_THIRD_PERSON_CAM_ORBIT_DISTANCE_LIMITS_THIS_UPDATE(0.0f, g_camZoom);
	}

	// Top-Down Cam: a scripted camera straight above the player, higher
	// while sprinting or riding fast.
	Cam g_topCam = 0;
	float g_topHeight = 20.0f;

	void TopDownTick()
	{
		const Ped ped = Me();
		if (!CAMERA::DOES_CAM_EXIST(g_topCam))
		{
			g_topCam = CAMERA::CREATE_CAM("DEFAULT_SCRIPTED_CAMERA", TRUE);
			CAMERA::SET_CAM_ACTIVE(g_topCam, TRUE);
			CAMERA::RENDER_SCRIPT_CAMS(TRUE, TRUE, 3000, TRUE, FALSE, 0);
		}
		float target = 19.0f;
		const Entity mount = PED::IS_PED_ON_FOOT(ped) ? 0 : (PED::IS_PED_ON_MOUNT(ped) ? PED::GET_MOUNT(ped) : PED::GET_VEHICLE_PED_IS_USING(ped));
		if (mount && ENTITY::DOES_ENTITY_EXIST(mount))
			target = ENTITY::GET_ENTITY_SPEED(mount) > 30.0f ? 26.0f : 22.0f;
		else if (TASK::IS_PED_SPRINTING(ped))
			target = 22.0f;
		if (g_topHeight < target) g_topHeight = (std::min)(target, g_topHeight + 0.14f);
		else if (g_topHeight > target) g_topHeight = (std::max)(target, g_topHeight - 0.14f);
		const Vector3 p = ENTITY::GET_ENTITY_COORDS(ped, FALSE, FALSE);
		CAMERA::SET_CAM_COORD(g_topCam, p.x, p.y, p.z + g_topHeight);
		CAMERA::SET_CAM_ROT(g_topCam, -90.0f, 0.0f, CAMERA::GET_GAMEPLAY_CAM_ROT(2).z, 2);
	}

	void TopDownOff()
	{
		if (CAMERA::DOES_CAM_EXIST(g_topCam))
		{
			CAMERA::SET_CAM_ACTIVE(g_topCam, FALSE);
			CAMERA::DESTROY_CAM(g_topCam, FALSE);
			CAMERA::RENDER_SCRIPT_CAMS(FALSE, TRUE, 3000, TRUE, FALSE, 0);
		}
		g_topCam = 0;
	}

	void SetFreezeCam(bool on)
	{
		if (on)
			GRAPHICS::TOGGLE_PAUSED_RENDERPHASES(FALSE);
		else
		{
			GRAPHICS::TOGGLE_PAUSED_RENDERPHASES(TRUE);
			GRAPHICS::RESET_PAUSED_RENDERPHASES();
		}
	}

	// No Clip: moves the player (or their mount) along the camera's
	// direction with W/S or the left stick.
	float g_noClipSpeed = 1.0f;

	void NoClipTick()
	{
		const Ped ped = Me();
		const Entity e = PED::IS_PED_ON_MOUNT(ped) ? PED::GET_MOUNT(ped) : ped;
		Vector3 p = ENTITY::GET_ENTITY_COORDS(e, FALSE, FALSE);
		const Vector3 rot = CAMERA::GET_GAMEPLAY_CAM_ROT(2);
		const Vector3 f = CamForward(rot);
		ENTITY::SET_ENTITY_VELOCITY(e, 0.0f, 0.0f, 0.0f);
		ENTITY::SET_ENTITY_HEADING(e, rot.z);
		float speed = g_noClipSpeed;
		if (IsKeyDown(VK_SHIFT) || PAD::IS_DISABLED_CONTROL_PRESSED(0, INPUT_SPRINT))
			speed *= 3.0f;
		if (IsKeyDown('W') || PAD::IS_DISABLED_CONTROL_PRESSED(0, INPUT_MOVE_UP_ONLY))
			p = { p.x + f.x * speed, p.y + f.y * speed, p.z + f.z * speed };
		if (IsKeyDown('S') || PAD::IS_DISABLED_CONTROL_PRESSED(0, INPUT_MOVE_DOWN_ONLY))
			p = { p.x - f.x * speed, p.y - f.y * speed, p.z - f.z * speed };
		ENTITY::SET_ENTITY_COORDS_NO_OFFSET(e, p.x, p.y, p.z, TRUE, FALSE, TRUE);
	}

	// Air Walk (ours): the player walks on air at a fixed height; Shift /
	// LCtrl (or attack / aim on a pad) raise and lower it.
	bool g_airWalkSet = false;
	float g_airWalkZ = 0.0f;

	void AirWalkTick()
	{
		const Ped ped = Me();
		const Entity e = PED::IS_PED_IN_ANY_VEHICLE(ped, TRUE) ? PED::GET_VEHICLE_PED_IS_IN(ped, TRUE) : ped;
		const Vector3 p = ENTITY::GET_ENTITY_COORDS(e, TRUE, FALSE);
		if (!g_airWalkSet)
		{
			g_airWalkZ = p.z;
			g_airWalkSet = true;
		}
		if (IsKeyDown(VK_SHIFT) || PAD::IS_CONTROL_PRESSED(2, INPUT_ATTACK))
			g_airWalkZ += 0.1f;
		if (IsKeyDown(VK_LCONTROL) || PAD::IS_CONTROL_PRESSED(2, INPUT_AIM))
			g_airWalkZ -= 0.1f;
		if (p.z < g_airWalkZ || IsKeyDown(VK_LCONTROL) || PAD::IS_CONTROL_PRESSED(2, INPUT_AIM))
		{
			const Vector3 v = ENTITY::GET_ENTITY_VELOCITY(e, 0);
			ENTITY::SET_ENTITY_COORDS_NO_OFFSET(e, p.x, p.y, g_airWalkZ, FALSE, FALSE, FALSE);
			ENTITY::SET_ENTITY_VELOCITY(e, v.x, v.y, 0.0f);
		}
	}

	// Free Cam: the camera flies on its own while the player stays put; a
	// hidden object carries the streaming focus.
	Cam g_freeCam = 0;
	Object g_freeCamFocus = 0;
	float g_freeCamSpeed = 0.5f;

	void FreeCamTick()
	{
		if (!CAMERA::DOES_CAM_EXIST(g_freeCam))
		{
			const Vector3 p = CAMERA::GET_GAMEPLAY_CAM_COORD();
			const Vector3 r = CAMERA::GET_GAMEPLAY_CAM_ROT(2);
			g_freeCam = CAMERA::CREATE_CAM("DEFAULT_SCRIPTED_CAMERA", TRUE);
			CAMERA::SET_CAM_COORD(g_freeCam, p.x, p.y, p.z);
			CAMERA::SET_CAM_ROT(g_freeCam, r.x, 0.0f, r.z, 2);
			CAMERA::SET_CAM_FOV(g_freeCam, 73.0f);
			CAMERA::SET_CAM_ACTIVE(g_freeCam, TRUE);
			CAMERA::RENDER_SCRIPT_CAMS(TRUE, FALSE, 2000, TRUE, FALSE, 0);
			const Hash model = GameUtil::Joaat("p_bottle01x"); // any small prop
			if (GameUtil::LoadModel(model))
			{
				g_freeCamFocus = OBJECT::CREATE_OBJECT(model, p.x, p.y, p.z, FALSE, FALSE, FALSE, FALSE, FALSE);
				ENTITY::SET_ENTITY_VISIBLE(g_freeCamFocus, FALSE);
				ENTITY::SET_ENTITY_COLLISION(g_freeCamFocus, FALSE, TRUE);
				ENTITY::FREEZE_ENTITY_POSITION(g_freeCamFocus, TRUE);
				STREAMING::SET_FOCUS_ENTITY(g_freeCamFocus);
				STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
			}
		}
		PAD::DISABLE_ALL_CONTROL_ACTIONS(0);
		Vector3 rot = CAMERA::GET_CAM_ROT(g_freeCam, 2);
		rot.z -= PAD::GET_DISABLED_CONTROL_NORMAL(0, INPUT_LOOK_LR) * 5.0f;
		rot.x = std::clamp(rot.x - PAD::GET_DISABLED_CONTROL_NORMAL(0, INPUT_LOOK_UD) * 5.0f, -85.0f, 85.0f);
		const Vector3 f = CamForward(rot);
		float speed = g_freeCamSpeed;
		if (PAD::IS_DISABLED_CONTROL_PRESSED(0, INPUT_SPRINT))
			speed *= 4.0f;
		const float forward = -PAD::GET_DISABLED_CONTROL_NORMAL(0, INPUT_MOVE_UD) * speed;
		const float right = PAD::GET_DISABLED_CONTROL_NORMAL(0, INPUT_MOVE_LR) * speed;
		Vector3 p = CAMERA::GET_CAM_COORD(g_freeCam);
		p.x += f.x * forward + f.y * right;
		p.y += f.y * forward - f.x * right;
		p.z += f.z * forward;
		CAMERA::SET_CAM_COORD(g_freeCam, p.x, p.y, p.z);
		CAMERA::SET_CAM_ROT(g_freeCam, rot.x, 0.0f, rot.z, 2);
		if (ENTITY::DOES_ENTITY_EXIST(g_freeCamFocus))
			ENTITY::SET_ENTITY_COORDS_NO_OFFSET(g_freeCamFocus, p.x, p.y, p.z, FALSE, FALSE, FALSE);
	}

	void FreeCamOff()
	{
		if (CAMERA::DOES_CAM_EXIST(g_freeCam))
		{
			CAMERA::SET_CAM_ACTIVE(g_freeCam, FALSE);
			CAMERA::DESTROY_CAM(g_freeCam, FALSE);
			CAMERA::RENDER_SCRIPT_CAMS(FALSE, FALSE, 2000, TRUE, FALSE, 0);
		}
		g_freeCam = 0;
		STREAMING::CLEAR_FOCUS();
		if (ENTITY::DOES_ENTITY_EXIST(g_freeCamFocus))
		{
			ENTITY::SET_ENTITY_AS_MISSION_ENTITY(g_freeCamFocus, TRUE, TRUE);
			OBJECT::DELETE_OBJECT(&g_freeCamFocus);
		}
		g_freeCamFocus = 0;
	}

	// --- world switches ------------------------------------------------------------

	void SetPhotoModePause(bool on)
	{
		if (on)
			ANIMSCENE::_REQUEST_PHOTO_MODE_FREEZE();
		else
			ANIMSCENE::_REQUEST_PHOTO_MODE_DEFREEZE();
	}

	void SetDecreaseGraphics(bool on)
	{
		if (on)
		{
			GRAPHICS::SET_TIMECYCLE_MODIFIER("LODmult_HD_orphan_LOD_reduce");
			GRAPHICS::SET_TIMECYCLE_MODIFIER_STRENGTH(1000.0f);
		}
		else
		{
			GRAPHICS::SET_TIMECYCLE_MODIFIER_STRENGTH(1.0f);
			GRAPHICS::CLEAR_TIMECYCLE_MODIFIER();
		}
	}

	// New Austin sniper: medium_update fires when GET_GAME_TIMER() -
	// Global_1879534.f_44 exceeds a random 1-5 s, so keeping the stamp in
	// the future keeps it quiet.
	void NoNewAustinSniperTick()
	{
		if (SCRIPT::IS_LOADING_SCREEN_VISIBLE())
			return;
		if (UINT64* stamp = GameUtil::Global(1879534 + 44))
			*reinterpret_cast<int*>(stamp) = MISC::GET_GAME_TIMER() + 6000;
	}

	// Guarma sniper: region_law_guama_fussar fires when iLocal_276 is in
	// the past (see CLAUDE.md).
	void NoGuarmaSniperTick()
	{
		if (SCRIPT::IS_LOADING_SCREEN_VISIBLE())
			return;
		static const Hash script = GameUtil::Joaat("region_law_guama_fussar");
		if (!SCRIPT::GET_NUMBER_OF_THREADS_RUNNING_THE_SCRIPT_WITH_THIS_HASH(script))
			return;
		if (rage::scrThread* thread = GamePointers::FindScriptThread(script))
			if (std::uint64_t* local = GamePointers::ScriptLocal(thread, 276))
				*reinterpret_cast<int*>(local) = MISC::GET_GAME_TIMER() + 6000;
	}

	void NoGuardZonesTick()
	{
		for (const char* zone : kGuardZones)
		{
			LAW::_DISABLE_GUARD_ZONE(zone);
			LAW::_REMOVE_GUARD_ZONE(zone);
		}
	}

	constexpr int kResetFlagNoDrown = 364; // Rampage's reset flag; also keeps boats afloat

	std::string TakePhoto()
	{
		GRAPHICS::FREE_MEMORY_FOR_HIGH_QUALITY_PHOTO();
		if (!GRAPHICS::BEGIN_TAKE_HIGH_QUALITY_PHOTO())
			return "Couldn't start the photo";
		const DWORD start = GetTickCount();
		int status = 1;
		while ((status = GRAPHICS::GET_STATUS_OF_TAKE_HIGH_QUALITY_PHOTO()) == 1 && GetTickCount() - start < 3000)
			WAIT(0);
		if (status != 0)
			return "Photo failed";
		GRAPHICS::SAVE_HIGH_QUALITY_PHOTO(0);
		return "Photo saved";
	}

	bool PlayMusicEvent(const char* name)
	{
		AUDIO::TRIGGER_MUSIC_EVENT("MC_MUSIC_STOP");
		const DWORD start = GetTickCount();
		while (!AUDIO::PREPARE_MUSIC_EVENT(name) && GetTickCount() - start < 5000)
			WAIT(0);
		return AUDIO::TRIGGER_MUSIC_EVENT(name);
	}

	// Rampage's fix: play a music event and flash a loading screen, which
	// makes the mission flow re-place its blips.
	std::string FixMissionMarkers()
	{
		PlayMusicEvent("TRN4_START");
		WAIT(500);
		SCRIPT::_DISPLAY_LOADING_SCREENS(0, 0, 0, "Rampagio", "Fixing mission markers", "");
		SCRIPT::SHUTDOWN_LOADING_SCREEN();
		const DWORD start = GetTickCount();
		while (SCRIPT::IS_LOADING_SCREEN_VISIBLE() && GetTickCount() - start < 10000)
			WAIT(0);
		AUDIO::TRIGGER_MUSIC_EVENT("MC_MUSIC_STOP");
		WAIT(500);
		return "";
	}

	// Dev toggles: replace a native for every game script.
	void ReturnTrue(rage::scrNativeCallContext* ctx) { ctx->SetReturnValue<BOOL>(TRUE); }

	constexpr std::uint64_t kIsMagDemo1Active = 0x5FC9357C26DAEFCE;   // IS_MAG_DEMO_1_ACTIVE
	constexpr std::uint64_t kProfanityPassed = 0xF302973BB8BE70E6;    // SC_PROFANITY_GET_STRING_PASSED
	constexpr std::uint64_t kIsDlcPresent = 0x2763DC12BBE2BB6F;       // IS_DLC_PRESENT

	std::function<void(bool)> HookToggle(std::uint64_t native)
	{
		auto id = std::make_shared<NativeHooks::Id>(0);
		return [native, id](bool on)
		{
			NativeHooks::Remove(*id);
			*id = on ? NativeHooks::Add(NativeHooks::kAllScripts, native, ReturnTrue) : 0;
		};
	}

	// --- game music ------------------------------------------------------------------

	void BuildGameMusic(MenuBase* misc)
	{
		MenuBase* music = Ui::Submenu(misc, "Game Music");
		Ui::Do(music, "Stop Music Events", [] { AUDIO::TRIGGER_MUSIC_EVENT("MC_MUSIC_STOP"); });
		Ui::Toggle(music, "Disable Idle Music", [](bool on) { AUDIO::SET_AUDIO_FLAG("EnableIdleMusic", !on); });
		Ui::Toggle(music, "Disable Cutscene Music", [](bool on) { AUDIO::SET_AUDIO_FLAG("EnableCutsceneMusic", !on); });
		Ui::Toggle(music, "Suppress Train Whistles", [](bool on) { AUDIO::SET_AUDIO_FLAG("SuppressNewAndExistingTrainWhistles", on); });
		Ui::Action(music, "Load Audio Bank", []() -> std::string
		{
			std::string bank;
			if (!GameUtil::PromptText("Audio Bank:", bank) || bank.empty())
				return "";
			return AUDIO::REQUEST_SCRIPT_AUDIO_BANK(bank.c_str()) ? "Loaded" : "Not loaded (yet)";
		});
		Ui::NameList(music, "Music Events", kMusicEvents, [](const std::string& name)
		{
			if (!PlayMusicEvent(name.c_str()))
				Log::Write("[Music] {} didn't trigger", name);
		});
	}

	// --- music player (ours: Windows MCI) ---------------------------------------------

	std::wstring MusicFolder() { return LogFallback::ModuleDirectory() + L"RampagioMusic\\"; }

	std::vector<std::filesystem::path> MusicFiles()
	{
		std::vector<std::filesystem::path> files;
		std::error_code ec;
		for (const auto& entry : std::filesystem::directory_iterator(MusicFolder(), ec))
		{
			std::wstring ext = entry.path().extension().wstring();
			std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
			if (ext == L".mp3" || ext == L".wav" || ext == L".wma" || ext == L".m4a" || ext == L".aac" || ext == L".flac")
				files.push_back(entry.path());
		}
		std::sort(files.begin(), files.end());
		return files;
	}

	struct MusicPlayer
	{
		bool open = false;
		bool paused = false;
		bool loop = false;
		bool playAll = false;
		int volume = 100; // percent
		std::filesystem::path current;

		static std::wstring Send(const std::wstring& cmd)
		{
			wchar_t out[128] = {};
			mciSendStringW(cmd.c_str(), out, 128, nullptr);
			return out;
		}

		void Close()
		{
			if (open)
				Send(L"close rampagio_music");
			open = false;
			paused = false;
		}

		bool Play(const std::filesystem::path& file)
		{
			Close();
			if (mciSendStringW(std::format(L"open \"{}\" type mpegvideo alias rampagio_music", file.wstring()).c_str(), nullptr, 0, nullptr) != 0)
				return false;
			open = true;
			current = file;
			Send(L"set rampagio_music time format milliseconds");
			ApplyVolume();
			Send(L"play rampagio_music");
			return true;
		}

		void ApplyVolume()
		{
			if (open)
				Send(std::format(L"setaudio rampagio_music volume to {}", volume * 10));
		}

		void Seek(int deltaMs)
		{
			if (!open)
				return;
			const int length = _wtoi(Send(L"status rampagio_music length").c_str());
			const int pos = std::clamp(_wtoi(Send(L"status rampagio_music position").c_str()) + deltaMs, 0, (std::max)(0, length - 1));
			Send(std::format(L"seek rampagio_music to {}", pos));
			if (!paused)
				Send(L"play rampagio_music");
		}

		// Restarts or advances once the track ends.
		void Tick()
		{
			if (!open || paused || Send(L"status rampagio_music mode") != L"stopped")
				return;
			if (loop)
			{
				Send(L"seek rampagio_music to start");
				Send(L"play rampagio_music");
				return;
			}
			if (!playAll)
				return;
			const auto files = MusicFiles();
			if (files.empty())
				return Close();
			auto it = std::upper_bound(files.begin(), files.end(), current);
			Play(it == files.end() ? files.front() : *it);
		}

		std::string Status() const
		{
			if (!open)
				return "Nothing";
			return current.filename().string() + (paused ? " [Paused]" : "");
		}
	} g_player;

	MenuBase* g_musicFiles = nullptr;

	void BuildMusicPlayer(MenuBase* misc)
	{
		MenuBase* player = Ui::Submenu(misc, "Music Player");
		Ui::Action(player, "Now Playing", [] { return "Playing: " + g_player.Status(); });
		Ui::Do(player, "Pause", [] { if (g_player.open) { MusicPlayer::Send(L"pause rampagio_music"); g_player.paused = true; } });
		Ui::Do(player, "Resume", [] { if (g_player.open) { MusicPlayer::Send(L"resume rampagio_music"); g_player.paused = false; } });
		Ui::Do(player, "Stop", [] { g_player.Close(); });
		Ui::Toggle(player, "Loop", [](bool on) { g_player.loop = on; }, [] { g_player.Tick(); });
		Ui::Toggle(player, "Play All", [](bool on) { g_player.playAll = on; }, [] { g_player.Tick(); });
		Ui::Number(player, "Volume (%)", &g_player.volume, 0, 100, 5, [] { g_player.ApplyVolume(); });
		Ui::Do(player, "Forward 15s", [] { g_player.Seek(15000); });
		Ui::Do(player, "Back 15s", [] { g_player.Seek(-15000); });
		g_musicFiles = Ui::ListMenu(player, "Files", [](MenuBase* menu)
		{
			const auto files = MusicFiles();
			if (files.empty())
			{
				Ui::Action(menu, "No files in RampagioMusic", [] { return "Put .mp3/.wav/.wma files in a RampagioMusic folder next to Rampagio.asi"; });
				return;
			}
			for (const auto& file : files)
				Ui::Action(menu, file.filename().string(), [file]() -> std::string
				{
					return g_player.Play(file) ? "" : "Windows couldn't play this file";
				});
		});
	}

	// --- mobile theater ------------------------------------------------------------------

	float g_tvVolume = 0.0f; // dB-ish: SET_TV_VOLUME's range is about -36..0
	float g_tvX = 0.82f, g_tvY = 0.22f, g_tvScale = 0.3f;

	void TheaterTick()
	{
		GRAPHICS::DRAW_TV_CHANNEL(g_tvX, g_tvY, g_tvScale, g_tvScale, 0.0f, 255, 255, 255, 255);
	}

	void TheaterOff()
	{
		GRAPHICS::SET_TV_CHANNEL_PLAYLIST(0, "", TRUE);
		GRAPHICS::SET_TV_CHANNEL(-1);
	}

	void PlayShow(const char* playlist)
	{
		GRAPHICS::SET_TV_AUDIO_FRONTEND(FALSE);
		GRAPHICS::SET_TV_VOLUME(g_tvVolume);
		GRAPHICS::ATTACH_TV_AUDIO_TO_ENTITY(Me());
		GRAPHICS::SET_TV_CHANNEL(-1);
		GRAPHICS::SET_TV_CHANNEL_PLAYLIST(1, playlist, TRUE);
		GRAPHICS::SET_TV_CHANNEL(1);
	}

	void BuildTheater(MenuBase* misc)
	{
		MenuBase* tv = Ui::Submenu(misc, "Mobile Theater");
		Ui::Looped(tv, "Enable Theater", TheaterTick, TheaterOff);
		Ui::Number(tv, "Volume", &g_tvVolume, -36.0f, 0.0f, 1.0f, [] { GRAPHICS::SET_TV_VOLUME(g_tvVolume); });
		Ui::Number(tv, "Screen X", &g_tvX, 0.0f, 1.0f, 0.01f);
		Ui::Number(tv, "Screen Y", &g_tvY, 0.0f, 1.0f, 0.01f);
		Ui::Number(tv, "Screen Size", &g_tvScale, 0.1f, 1.0f, 0.01f);
		Ui::Section(tv, "Shows");
		static const std::pair<const char*, const char*> kShows[] = {
			{ "Modern Medicine", "PL_TOON_MODERN_MEDICINE" },
			{ "The Farmer's Daughter", "PL_TOON_FARMERS_DAUGHTER" },
			{ "Sketching for Sweetheart", "PL_TOON_SKETCHING_FOR_SWEETHEART" },
			{ "World's Strongest Man", "PL_TOON_WORLDS_STRONGEST_MAN" },
			{ "Direct Current Damnation", "PL_TOON_DIRECT_CURRENT_DAMNATION" },
			{ "Bear", "PL_MLAN_BEAR" },
			{ "The Secret of Manflight", "PL_MLAN_SECRET_OF_MANFLIGHT" },
			{ "Josiah Blackwater", "PL_MLAN_JOSIAH_BLACKWATER" },
			{ "Saviors and Savages", "PL_MLAN_SAVIORS_AND_SAVAGES" },
			{ "Ghost Story", "PL_MLAN_GHOST_STORY" },
		};
		for (const auto& [caption, playlist] : kShows)
			Ui::Do(tv, caption, [playlist] { PlayShow(playlist); });
	}

	// --- cutscene player -----------------------------------------------------------------

	AnimScene g_cutscene = 0;

	// The cutscene the game itself is playing: missions keep it in Global_43800.
	AnimScene GameCutscene()
	{
		if (SCRIPT::IS_LOADING_SCREEN_VISIBLE())
			return 0;
		const UINT64* g = GameUtil::Global(43800);
		return g ? static_cast<AnimScene>(*g) : 0;
	}

	AnimScene CurrentCutscene()
	{
		const AnimScene game = GameCutscene();
		if (game > 0 && ANIMSCENE::DOES_ANIM_SCENE_EXIST(game))
			return game;
		return g_cutscene;
	}

	void StopOurCutscene()
	{
		if (g_cutscene && ANIMSCENE::DOES_ANIM_SCENE_EXIST(g_cutscene))
		{
			ANIMSCENE::ABORT_ANIM_SCENE(g_cutscene, FALSE);
			ANIMSCENE::_DELETE_ANIM_SCENE(g_cutscene);
			AUDIO::TRIGGER_MUSIC_EVENT("MC_MUSIC_STOP");
		}
		g_cutscene = 0;
	}

	std::string PlayCutscene(const std::string& name)
	{
		if (MISC::IS_MINIGAME_IN_PROGRESS())
			return "Can't load a cutscene during a minigame";
		StopOurCutscene();
		const std::string dict = name.starts_with("cutscene@") ? name : "cutscene@" + name;
		g_cutscene = ANIMSCENE::_CREATE_ANIM_SCENE(dict.c_str(), 0, nullptr, FALSE, TRUE);
		if (!ANIMSCENE::DOES_ANIM_SCENE_EXIST(g_cutscene))
			return "Cutscene is invalid";
		ANIMSCENE::LOAD_ANIM_SCENE(g_cutscene);
		const DWORD start = GetTickCount();
		while (!ANIMSCENE::IS_ANIM_SCENE_LOADED(g_cutscene, TRUE, FALSE))
		{
			if (GetTickCount() - start > 10000)
			{
				StopOurCutscene();
				return "Failed to load";
			}
			WAIT(0);
		}
		if (ANIMSCENE::IS_ANIM_SCENE_METADATA_LOADED(g_cutscene, FALSE))
			for (const char* id : { "ARTHUR", "player_zero", "player_three" })
				if (ANIMSCENE::_DOES_ENTITY_WITH_ID_EXIST_IN_ANIM_SCENE(g_cutscene, id))
				{
					ANIMSCENE::SET_ANIM_SCENE_ENTITY(g_cutscene, id, Me(), 0);
					break;
				}
		ANIMSCENE::START_ANIM_SCENE(g_cutscene);
		return "";
	}

	void BuildCutscenePlayer(MenuBase* misc)
	{
		MenuBase* cs = Ui::Submenu(misc, "Cutscene Player");
		Ui::Toggle(cs, "Pause", [](bool on)
		{
			if (const AnimScene scene = CurrentCutscene())
				ANIMSCENE::SET_ANIM_SCENE_PAUSED(scene, on);
		});
		Ui::Do(cs, "Stop Current", []
		{
			const AnimScene game = GameCutscene();
			if (game > 0 && ANIMSCENE::DOES_ANIM_SCENE_EXIST(game))
			{
				ANIMSCENE::ABORT_ANIM_SCENE(game, FALSE);
				ANIMSCENE::_DELETE_ANIM_SCENE(game);
				AUDIO::TRIGGER_MUSIC_EVENT("MC_MUSIC_STOP");
			}
			StopOurCutscene();
			CAMERA::DO_SCREEN_FADE_IN(0);
		});
		Ui::Do(cs, "Skip Current", []
		{
			if (const AnimScene scene = CurrentCutscene())
				ANIMSCENE::TRIGGER_ANIM_SCENE_SKIP(scene);
		});
		Ui::NameList(cs, "Cutscenes", kCutscenes, [](const std::string& name)
		{
			const std::string error = PlayCutscene(name);
			if (!error.empty())
				Log::Write("[Cutscene] {}: {}", name, error);
		});
	}

	// --- Undead Nightmare II (ours) ----------------------------------------------------

	constexpr Hash kZombieModel = 0x4C6C6086; // a_m_m_unicorpse_01
	constexpr Hash kVampireModel = 0xD95BCB7D; // cs_vampire
	constexpr Hash BLIP_STYLE_ENEMY = 0x318C617C;
	constexpr Hash BLIP_STYLE_ENEMY_SEVERE = 0x12BD604C;
	constexpr Hash WEATHER_THUNDERSTORM = 0x7C1C4A13;
	constexpr Hash WEATHER_SUNNY = 0x614A1F91;

	struct Undead
	{
		std::vector<Ped> peds;
		int kills = 0;
		int nextBoss = 25;
		int preset = 0;
		DWORD lastSpawn = 0;
	} g_undead;
	int g_undeadCount = 15;

	Ped SpawnHostile(Hash model, float radius)
	{
		if (!GameUtil::LoadModel(model))
			return 0;
		const Vector3 me = ENTITY::GET_ENTITY_COORDS(Me(), TRUE, FALSE);
		const float x = me.x + MISC::GET_RANDOM_FLOAT_IN_RANGE(-radius, radius);
		const float y = me.y + MISC::GET_RANDOM_FLOAT_IN_RANGE(-radius, radius);
		Vector3 safe{};
		if (!PATH::GET_SAFE_COORD_FOR_PED(x, y, me.z, TRUE, &safe, 16))
			safe = { x, y, me.z };
		const Ped ped = PED::CREATE_PED(model, safe.x, safe.y, safe.z, MISC::GET_RANDOM_FLOAT_IN_RANGE(0.0f, 360.0f), FALSE, TRUE, FALSE, FALSE);
		STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
		if (!ped)
			return 0;
		PED::_SET_RANDOM_OUTFIT_VARIATION(ped, TRUE);
		WEAPON::SET_PED_DROPS_WEAPONS_WHEN_DEAD(ped, FALSE);
		PED::SET_PED_COMBAT_ATTRIBUTES(ped, 17, FALSE); // don't flee
		PED::SET_PED_COMBAT_ATTRIBUTES(ped, 5, TRUE);   // always fight
		PED::SET_PED_COMBAT_ABILITY(ped, 1);
		DECORATOR::DECOR_SET_INT(ped, "honor_override", 0);
		TASK::TASK_COMBAT_PED(ped, Me(), 0, 16);
		PED::SET_PED_KEEP_TASK(ped, TRUE);
		PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(ped, TRUE);
		return ped;
	}

	void SpawnZombie()
	{
		const Ped z = SpawnHostile(kZombieModel, 50.0f);
		if (!z)
			return;
		PED::_EQUIP_META_PED_OUTFIT_PRESET(z, g_undead.preset++ % 0x90, FALSE);
		MAP::BLIP_ADD_FOR_ENTITY(BLIP_STYLE_ENEMY, z);
		PED::SET_PED_COMBAT_MOVEMENT(z, 3);
		PED::SET_PED_COMBAT_RANGE(z, 2);
		PED::SET_PED_MOVE_RATE_OVERRIDE(z, 0.6f);
		PED::_SET_PED_PROMPT_NAME(z, "Zombie");
		ENTITY::SET_ENTITY_HEALTH(z, MISC::GET_RANDOM_INT_IN_RANGE(200, 300), 0);
		PED::_SET_PED_DESIRED_LOCO_FOR_MODEL(z, "DEFAULT");
		PED::_SET_PED_DESIRED_LOCO_MOTION_TYPE(z, "very_drunk");
		g_undead.peds.push_back(z);
	}

	void SpawnBoss()
	{
		const bool vampire = MISC::GET_RANDOM_INT_IN_RANGE(0, 2) == 1;
		const Ped boss = SpawnHostile(vampire ? kVampireModel : kZombieModel, 5.0f);
		if (!boss)
			return;
		if (vampire)
			WEAPON::GIVE_DELAYED_WEAPON_TO_PED(boss, GameUtil::Joaat("WEAPON_MELEE_KNIFE_JAWBONE"), 1, FALSE, GameUtil::Joaat("ADD_REASON_DEFAULT"));
		else
			PED::_EQUIP_META_PED_OUTFIT_PRESET(boss, 0x90, FALSE);
		MAP::BLIP_ADD_FOR_ENTITY(BLIP_STYLE_ENEMY_SEVERE, boss);
		PED::_SET_PED_PROMPT_NAME(boss, vampire ? "Vampire" : "Frankmonster");
		PED::_SET_PED_SCALE(boss, vampire ? 1.3f : 1.0f);
		ENTITY::SET_ENTITY_HEALTH(boss, vampire ? 2000 : 4000, 0);
		g_undead.peds.push_back(boss);
	}

	void UndeadTick()
	{
		if (ENTITY::IS_ENTITY_DEAD(Me()))
			return;
		CLOCK::SET_CLOCK_TIME(23, 23, 0);
		auto& peds = g_undead.peds;
		for (auto it = peds.begin(); it != peds.end();)
		{
			if (!ENTITY::DOES_ENTITY_EXIST(*it) || ENTITY::IS_ENTITY_DEAD(*it))
			{
				if (ENTITY::DOES_ENTITY_EXIST(*it))
				{
					++g_undead.kills;
					Ped p = *it;
					ENTITY::SET_ENTITY_AS_NO_LONGER_NEEDED(&p);
				}
				it = peds.erase(it);
			}
			else
				++it;
		}
		if (g_undead.kills >= g_undead.nextBoss)
		{
			g_undead.nextBoss += 25;
			SpawnBoss();
		}
		const DWORD now = GetTickCount();
		if (static_cast<int>(peds.size()) < g_undeadCount && now - g_undead.lastSpawn > 750)
		{
			g_undead.lastSpawn = now;
			SpawnZombie();
		}
	}

	void SetUndead(bool on)
	{
		if (on)
		{
			g_undead = {};
			CLOCK::PAUSE_CLOCK(TRUE, 0);
			MISC::CLEAR_OVERRIDE_WEATHER();
			MISC::CLEAR_WEATHER_TYPE_PERSIST();
			MISC::SET_WEATHER_TYPE(WEATHER_THUNDERSTORM, TRUE, TRUE, FALSE, 0.0f, FALSE);
			PLAYER::SET_WANTED_LEVEL_MULTIPLIER(0.0f);
			return;
		}
		for (Ped p : g_undead.peds)
			if (ENTITY::DOES_ENTITY_EXIST(p))
			{
				ENTITY::SET_ENTITY_AS_MISSION_ENTITY(p, TRUE, TRUE);
				PED::DELETE_PED(&p);
			}
		Log::Write("[Undead] {} kills", g_undead.kills);
		g_undead.peds.clear();
		CLOCK::SET_CLOCK_TIME(12, 0, 0);
		CLOCK::PAUSE_CLOCK(FALSE, 0);
		MISC::CLEAR_OVERRIDE_WEATHER();
		MISC::CLEAR_WEATHER_TYPE_PERSIST();
		MISC::SET_WEATHER_TYPE(WEATHER_SUNNY, TRUE, TRUE, FALSE, 0.0f, FALSE);
		PLAYER::SET_WANTED_LEVEL_MULTIPLIER(1.0f);
	}

	// --- volume editor (ours: volumes made here) -----------------------------------------

	struct EditVolume
	{
		Volume handle;
		std::string shape;
		float pos[3];
		float scale[3];
		int rel = 0;
	};
	std::vector<EditVolume> g_volumes;
	size_t g_selVolume = 0;
	int g_volPrecision = 1; // index into kPrecision
	const float kPrecision[] = { 0.01f, 0.1f, 1.0f, 10.0f };
	MenuBase* g_volumeEdit = nullptr;

	EditVolume* SelectedVolume() { return g_selVolume < g_volumes.size() ? &g_volumes[g_selVolume] : nullptr; }

	void CreateVolume(const char* shape)
	{
		const Vector3 p = ENTITY::GET_ENTITY_COORDS(Me(), TRUE, FALSE);
		EditVolume v{ 0, shape, { p.x, p.y, p.z }, { 5.0f, 5.0f, 5.0f } };
		const std::string_view s = shape;
		v.handle = s == "Box" ? VOLUME::CREATE_VOLUME_BOX(p.x, p.y, p.z, 0, 0, 0, 5, 5, 5)
			: s == "Sphere" ? VOLUME::CREATE_VOLUME_SPHERE(p.x, p.y, p.z, 0, 0, 0, 5, 5, 5)
			: VOLUME::CREATE_VOLUME_CYLINDER(p.x, p.y, p.z, 0, 0, 0, 5, 5, 5);
		if (v.handle)
			g_volumes.push_back(v);
	}

	void BuildVolumeEdit(MenuBase* menu)
	{
		EditVolume* v = SelectedVolume();
		if (!v || !VOLUME::DOES_VOLUME_EXIST(v->handle))
		{
			Ui::Action(menu, "Volume is gone", [] { return ""; });
			return;
		}
		const Volume h = v->handle;
		std::vector<std::string> groups(std::begin(kRelGroups), std::end(kRelGroups));
		Ui::Choice(menu, "Relationship", groups, &v->rel, [h](int i) { VOLUME::_SET_VOLUME_RELATIONSHIP(h, GameUtil::Joaat(kRelGroups[i])); });
		const float step = kPrecision[g_volPrecision];
		Ui::Section(menu, "Position");
		auto move = [v] { VOLUME::SET_VOLUME_COORDS(v->handle, v->pos[0], v->pos[1], v->pos[2]); };
		Ui::Number(menu, "X Axis", &v->pos[0], -10000.0f, 10000.0f, step, move);
		Ui::Number(menu, "Y Axis", &v->pos[1], -10000.0f, 10000.0f, step, move);
		Ui::Number(menu, "Z Axis", &v->pos[2], -1000.0f, 3000.0f, step, move);
		Ui::Section(menu, "Scale");
		auto resize = [v] { VOLUME::SET_VOLUME_SCALE(v->handle, v->scale[0], v->scale[1], v->scale[2]); };
		Ui::Number(menu, "X Scale", &v->scale[0], 0.0f, 1000.0f, step, resize);
		Ui::Number(menu, "Y Scale", &v->scale[1], 0.0f, 1000.0f, step, resize);
		Ui::Number(menu, "Z Scale", &v->scale[2], 0.0f, 1000.0f, step, resize);
		Ui::Action(menu, "Delete", []
		{
			if (EditVolume* sel = SelectedVolume())
			{
				VOLUME::DELETE_VOLUME(sel->handle);
				g_volumes.erase(g_volumes.begin() + g_selVolume);
			}
			Ui::Controller().PopMenu();
			return std::string("Deleted");
		});
	}

	void BuildVolumeEditor(MenuBase* misc)
	{
		g_volumeEdit = Ui::DetachedListMenu("Edit Volume", BuildVolumeEdit);
		Ui::ListMenu(misc, "Volume Editor", [](MenuBase* menu)
		{
			Ui::Choice(menu, "Precision", { "0.01", "0.1", "1", "10" }, &g_volPrecision);
			for (const char* shape : { "Box", "Sphere", "Cylinder" })
				Ui::Do(menu, std::string("Create ") + shape + " at Player", [shape] { CreateVolume(shape); });
			std::erase_if(g_volumes, [](const EditVolume& v) { return !VOLUME::DOES_VOLUME_EXIST(v.handle); });
			Ui::Section(menu, std::format("Volumes ({})", g_volumes.size()));
			for (size_t i = 0; i < g_volumes.size(); ++i)
				Ui::Do(menu, std::format("{} #{}", g_volumes[i].shape, g_volumes[i].handle), [i]
				{
					g_selVolume = i;
					Ui::Push(g_volumeEdit);
				});
		});
	}
	// --- stat editor -----------------------------------------------------------------------

	// A stat is a base name plus an optional permutation (an item, horse,
	// ...): the script StatId struct.
	int g_statType = 0; // Int, Float, Bool
	std::string g_statBase, g_statPermutation;

	GameUtil::StatId CurrentStat()
	{
		return { GameUtil::ParseHash(g_statBase), GameUtil::ParseHash(g_statPermutation) };
	}

	std::string GetStat()
	{
		if (g_statBase.empty())
			return "Enter a base stat name first";
		GameUtil::StatId id = CurrentStat();
		switch (g_statType)
		{
		case 1: { float v = 0; if (!STATS::STAT_ID_GET_FLOAT(id.Ptr(), &v)) break; return std::format("{}", v); }
		case 2: { BOOL v = 0; if (!STATS::STAT_ID_GET_BOOL(id.Ptr(), &v)) break; return v ? "true" : "false"; }
		default: { int v = 0; if (!STATS::STAT_ID_GET_INT(id.Ptr(), &v)) break; return std::to_string(v); }
		}
		return "No such stat";
	}

	std::string SetStat()
	{
		if (g_statBase.empty())
			return "Enter a base stat name first";
		std::string text;
		if (!GameUtil::PromptText("Value:", text) || text.empty())
			return "";
		GameUtil::StatId id = CurrentStat();
		bool ok = false;
		try
		{
			switch (g_statType)
			{
			case 1: ok = STATS::STAT_ID_SET_FLOAT(id.Ptr(), std::stof(text), TRUE); break;
			case 2: ok = STATS::STAT_ID_SET_BOOL(id.Ptr(), text == "1" || text == "true", TRUE); break;
			default: ok = STATS::STAT_ID_SET_INT(id.Ptr(), std::stoi(text), TRUE); break;
			}
		}
		catch (const std::exception&)
		{
			return "Not a number";
		}
		return ok ? "Now " + GetStat() : "Couldn't set it";
	}

	void BuildStatEditor(MenuBase* misc)
	{
		MenuBase* stats = Ui::Submenu(misc, "Stat Editor");
		Ui::Choice(stats, "Type", { "Int", "Float", "Bool" }, &g_statType);
		Ui::Text(stats, "Base Stat Name", &g_statBase);
		Ui::Text(stats, "Permutation Stat Name", &g_statPermutation);
		Ui::Action(stats, "Get", GetStat);
		Ui::Action(stats, "Set", SetStat);
		Ui::Do(stats, "Reset", [] { g_statBase.clear(); g_statPermutation.clear(); g_statType = 0; });
	}
}

namespace Menus
{
	void BuildMiscellaneous(MenuBase* root)
	{
		MenuBase* misc = Ui::Submenu(root, "Miscellaneous");
		BuildMusicPlayer(misc);
		BuildGameMusic(misc);
		BuildTheater(misc);
		BuildCutscenePlayer(misc);
		MenuBase* minigames = Ui::Submenu(misc, "Minigames");
		Ui::Toggle(minigames, "Undead Nightmare II", SetUndead, UndeadTick);
		Ui::Number(minigames, "Undead at Once", &g_undeadCount, 1, 60, 1);

		Ui::Section(misc, "Camera");
		Ui::Looped(misc, "Cam Zoom", CamZoomTick);
		Ui::Number(misc, "Cam Zoom Distance", &g_camZoom, 1.0f, 100.0f, 1.0f);
		Ui::Looped(misc, "Top-Down Cam", TopDownTick, TopDownOff);
		Ui::Toggle(misc, "Freeze Cam", SetFreezeCam);
		Ui::Looped(misc, "No Clip", NoClipTick);
		Ui::Number(misc, "No Clip Speed", &g_noClipSpeed, 0.1f, 10.0f, 0.1f);
		Ui::Looped(misc, "Air Walk", AirWalkTick, [] { g_airWalkSet = false; });
		Ui::Looped(misc, "Free Cam", FreeCamTick, FreeCamOff);
		Ui::Number(misc, "Free Cam Speed", &g_freeCamSpeed, 0.1f, 5.0f, 0.1f);

		Ui::Section(misc, "Game");
		Ui::Toggle(misc, "Pause Game", [](bool on) { MISC::SET_GAME_PAUSED(on); });
		Ui::Toggle(misc, "Photo Mode Pause", SetPhotoModePause);
		Ui::Toggle(misc, "Decrease Graphics", SetDecreaseGraphics);
		Ui::Looped(misc, "Disable New Austin Sniper", NoNewAustinSniperTick);
		Ui::Looped(misc, "Disable Guarma Sniper", NoGuarmaSniperTick);
		Ui::Looped(misc, "Disable Guard Zones", NoGuardZonesTick);
		Ui::Looped(misc, "Disable Water Drown/Kill", [] { PED::SET_PED_RESET_FLAG(Me(), kResetFlagNoDrown, TRUE); });
		Ui::Action(misc, "Take a Photo", TakePhoto);
		Ui::Action(misc, "Fix Mission Markers", FixMissionMarkers);

		Ui::Section(misc, "Dev");
		BuildVolumeEditor(misc);
		BuildStatEditor(misc);
		Ui::Toggle(misc, "Enable Mag 1 Demo", HookToggle(kIsMagDemo1Active));
		Ui::Toggle(misc, "SC Profanity Bypass", HookToggle(kProfanityPassed));
		Ui::Toggle(misc, "Force All DLCs Present", HookToggle(kIsDlcPresent));
	}
}

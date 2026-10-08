/*
	Player animation, emote, effect and speech submenus: ports Rampage's
	Submenus::SubSelfAnimationsCustom, SubSelfAnimationsDicts and
	SubAnimationDictsList (Animations), SubSelfFacialAnimations, SubEffects,
	SubEmotes, SubPlaySpeech with its Regular / Flow Greet / Vignette /
	Custom lists, and SubVoiceChanger.

	Sources: the animation, speech, voice and effect lists come from the game
	scripts (tools/extract_player_lists.py), the emotes from alloc8or's
	eEmote enum (tools/extract_emotes.py), and the flag names are the game's
	eScriptedAnimFlags / eIkControlFlags as listed in Halen84's
	RDR3-Native-Flags-And-Enums (linked from alloc8or's TASK_PLAY_ANIM).
	Rampage's own preset tables (effects, regular speeches) aren't copied.
	Lists the user drops next to Rampagio.json extend the built-in ones:
	Rampagio_PedAnimList.txt ("dict anim" per line) and
	Rampagio_Speech{FlowGreets,Vignettes,List}.txt (one line name per line),
	the same formats as Rampage's PedAnimList.txt and Speech*.txt.
*/

#include "Menus.h"
#include "..\GameUtil.h"
#include "..\DataFile.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace
{
	// The Player menu's ped, or the Ped Editor's when reached through it
	// (Menus::Target).
	Ped Me() { return Menus::Target::Get(); }

	template <size_t N>
	std::span<const char* const> Names(const char* const (&names)[N]) { return names; }

	std::string Lower(std::string s)
	{
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return s;
	}

	struct NamePair
	{
		const char* first;
		const char* second;
	};

	struct EmoteName
	{
		const char* name;
		int type; // eEmoteType
	};

	const NamePair kAnimations[] = {
#include "..\data\Animations.inc"
	};
	const NamePair kEffects[] = {
#include "..\data\Effects.inc"
	};
	const EmoteName kEmotes[] = {
#include "..\data\Emotes.inc"
	};
	const char* const kSpeeches[] = {
#include "..\data\Speeches.inc"
	};
	const char* const kVoices[] = {
#include "..\data\Voices.inc"
	};

	// --- Animations -----------------------------------------------------

	// eScriptedAnimFlags, bit 0 first.
	const char* const kAnimFlags[] = {
		"AF_LOOPING", "AF_HOLD_LAST_FRAME", "AF_NOT_INTERRUPTABLE", "AF_UPPERBODY",
		"AF_SECONDARY", "AF_ABORT_ON_PED_MOVEMENT", "AF_ADDITIVE", "AF_OVERRIDE_PHYSICS",
		"AF_EXTRACT_INITIAL_OFFSET", "AF_EXIT_AFTER_INTERRUPTED", "AF_TAG_SYNC_IN", "AF_TAG_SYNC_OUT",
		"AF_TAG_SYNC_CONTINUOUS", "AF_FORCE_START", "AF_USE_KINEMATIC_PHYSICS", "AF_USE_MOVER_EXTRACTION",
		"AF_DONT_SUPPRESS_LOCO", "AF_ENDS_IN_DEAD_POSE", "AF_ACTIVATE_RAGDOLL_ON_COLLISION", "AF_DONT_EXIT_ON_DEATH",
		"AF_ABORT_ON_WEAPON_DAMAGE", "AF_DISABLE_FORCED_PHYSICS_UPDATE", "AF_GESTURE", "AF_SKIP_IF_BLOCKED_BY_HIGHER_PRIORITY_TASK",
		"AF_USE_ABSOLUTE_MOVER", "AF_0xC57F16E7", "AF_UPPERBODY_TAGS", "AF_PROCESS_ATTACHMENTS_ON_START",
		"AF_EXPAND_PED_CAPSULE_FROM_SKELETON", "AF_BLENDOUT_WRT_LAST_FRAME", "AF_DISABLE_PHYSICAL_ACTIVATION", "AF_DISABLE_RELEASE_EVENTS",
	};

	// eIkControlFlags, bit 0 first.
	const char* const kIkFlags[] = {
		"AIK_DISABLE_LEG_IK", "AIK_DISABLE_ARM_IK", "AIK_DISABLE_HEAD_IK", "AIK_DISABLE_TORSO_IK",
		"AIK_DISABLE_TORSO_REACT_IK", "AIK_USE_LEG_ALLOW_TAGS", "AIK_USE_LEG_BLOCK_TAGS", "AIK_USE_ARM_ALLOW_TAGS",
		"AIK_USE_ARM_BLOCK_TAGS", "AIK_PROCESS_WEAPON_HAND_GRIP", "AIK_USE_FP_ARM_LEFT", "AIK_USE_FP_ARM_RIGHT",
		"AIK_0x88FF50BE", "AIK_DISABLE_TORSO_VEHICLE_IK", "AIK_DISABLE_PRONE_IK", "AIK_UPPERBODY",
		"AIK_UPPERBODY_TAGS", "AIK_0xFCDC149B", "AIK_0x5465E64A", "AIK_DISABLE_LEG_POSTURE_IK",
		"AIK_0x32939A0E", "AIK_BLOCK_NON_ANIMSCENE_LOOKS", "AIK_0x3CC5DD38", "AIK_0xB819088C",
		"AIK_DISABLE_CONTOUR_IK", "AIK_0xF9E28A5F", "AIK_0x983AE6C1", "AIK_0x5B5D2BEF",
		"AIK_0xA4F64B54", "AIK_DISABLE_TWO_BONE_IK", "AIK_0x0C1380EC",
	};

	std::string g_animDict;
	std::string g_animName;
	std::string g_animFilter;
	int g_animFlag = 0;             // 0 = Custom Flags, else bit (g_animFlag - 1)
	std::string g_customFlags = "0";
	int g_ikFlag = 0;               // 0 = none, else bit (g_ikFlag - 1)
	std::string g_browseDict;       // the dictionary the Animations list shows

	std::vector<std::string> WithFirst(const char* first, std::span<const char* const> names)
	{
		std::vector<std::string> options{ first };
		options.insert(options.end(), names.begin(), names.end());
		return options;
	}

	// Decimal, or hex with 0x.
	int ParseFlags(const std::string& text)
	{
		try
		{
			return static_cast<int>(std::stoul(text, nullptr, 0));
		}
		catch (...)
		{
			return 0;
		}
	}

	int AnimFlags() { return g_animFlag > 0 ? 1 << (g_animFlag - 1) : ParseFlags(g_customFlags); }
	int IkFlags() { return g_ikFlag > 0 ? 1 << (g_ikFlag - 1) : 0; }

	// Rampage's call: blend 8 / -8, endless, playback rate 0.
	std::string PlayAnim(const std::string& dict, const std::string& anim)
	{
		if (dict.empty() || anim.empty())
			return "Set a dictionary and an animation first";
		if (!GameUtil::LoadAnimDict(dict.c_str()))
			return "Animation dictionary not found";
		TASK::TASK_PLAY_ANIM(Me(), dict.c_str(), anim.c_str(), 8.0f, -8.0f, -1, AnimFlags(), 0.0f, FALSE, IkFlags(), FALSE,
			g_animFilter.empty() ? nullptr : g_animFilter.c_str(), FALSE);
		return {};
	}

	void StopAnim() { TASK::STOP_ANIM_PLAYBACK(Me(), 0, FALSE); }

	void AddFlagRows(MenuBase* m)
	{
		Ui::Text(m, "Filter", &g_animFilter);
		Ui::Choice(m, "Playback Flags", WithFirst("Custom Flags", Names(kAnimFlags)), &g_animFlag);
		Ui::Text(m, "Custom Flags", &g_customFlags);
		Ui::Choice(m, "IK Flags", WithFirst("None", Names(kIkFlags)), &g_ikFlag);
	}

	// Dictionary (lowercase) -> its animations: the script list plus
	// Rampagio_PedAnimList.txt, re-read each time a list opens.
	std::map<std::string, std::vector<std::string>> AnimDictionaries()
	{
		std::map<std::string, std::vector<std::string>> dicts;
		auto add = [&dicts](std::string dict, std::string anim)
		{
			auto& anims = dicts[Lower(std::move(dict))];
			if (std::find(anims.begin(), anims.end(), anim) == anims.end())
				anims.push_back(std::move(anim));
		};
		for (const NamePair& a : kAnimations)
			add(a.first, a.second);
		for (const std::string& line : DataFile::LoadLines(L"Rampagio_PedAnimList.txt"))
		{
			const size_t space = line.find_first_of(" \t");
			const size_t anim = line.find_first_not_of(" \t", space);
			if (space != std::string::npos && anim != std::string::npos)
				add(line.substr(0, space), line.substr(anim));
		}
		return dicts;
	}

	MenuBase* g_dictMenu = nullptr; // set up in BuildAnimations

	void AddDictRow(MenuBase* m, const std::string& dict, size_t count)
	{
		Ui::Do(m, dict + " (" + std::to_string(count) + ")", [dict]
		{
			g_browseDict = dict;
			Ui::Push(g_dictMenu);
		});
	}

	// SubAnimationDictsList. Ours: picking an animation also fills in the
	// Custom Animations rows, so it can be replayed with other flags there.
	void BuildDictMenu(MenuBase* m)
	{
		Ui::Section(m, g_browseDict);
		AddFlagRows(m);
		Ui::Do(m, "Stop Animation", StopAnim);
		Ui::Section(m, "Animations");
		const auto dicts = AnimDictionaries();
		const auto it = dicts.find(g_browseDict);
		if (it == dicts.end())
			return;
		for (const std::string& anim : it->second)
			Ui::Action(m, anim, [anim]
			{
				g_animDict = g_browseDict;
				g_animName = anim;
				return PlayAnim(g_browseDict, anim);
			});
	}

	// --- Effects --------------------------------------------------------

	float g_effectScale = 1.0f;
	std::string g_effectAsset;
	std::string g_effectName;
	std::string g_loopAsset, g_loopName; // the last effect played, for Loop
	ULONGLONG g_lastLoop = 0;

	bool LoadPtfxAsset(const std::string& asset)
	{
		const Hash hash = GameUtil::Joaat(asset);
		STREAMING::REQUEST_NAMED_PTFX_ASSET(hash);
		for (int i = 0; i < 200 && !STREAMING::HAS_NAMED_PTFX_ASSET_LOADED(hash); i++)
			WAIT(10);
		return STREAMING::HAS_NAMED_PTFX_ASSET_LOADED(hash) != FALSE;
	}

	// Rampage's call: a random tint (only tintable effects use it), at the
	// player half a metre down.
	std::string PlayEffect(const std::string& asset, const std::string& fx, float scale)
	{
		if (asset.empty() || fx.empty())
			return "Set an asset and an effect first";
		if (!LoadPtfxAsset(asset))
			return "Particle asset not found";
		GRAPHICS::USE_PARTICLE_FX_ASSET(asset.c_str());
		GRAPHICS::SET_PARTICLE_FX_NON_LOOPED_COLOUR(MISC::GET_RANDOM_FLOAT_IN_RANGE(0.0f, 1.0f),
			MISC::GET_RANDOM_FLOAT_IN_RANGE(0.0f, 1.0f), MISC::GET_RANDOM_FLOAT_IN_RANGE(0.0f, 1.0f));
		GRAPHICS::START_PARTICLE_FX_NON_LOOPED_ON_ENTITY(fx.c_str(), Me(), 0.0f, 0.0f, -0.5f, 0.0f, 0.0f, 0.0f, scale, FALSE, FALSE, FALSE);
		g_loopAsset = asset;
		g_loopName = fx;
		return {};
	}

	// Rampage replays the last effect every 500 ms.
	void LoopEffectTick()
	{
		if (g_loopName.empty() || GetTickCount64() - g_lastLoop < 500)
			return;
		g_lastLoop = GetTickCount64();
		PlayEffect(g_loopAsset, g_loopName, g_effectScale);
	}

	// --- Emotes ---------------------------------------------------------

	// Rampage's defaults: full body, every option on.
	int g_emotePlayback = 2; // eEmotePlaybackMode
	bool g_emoteSecondary = true;
	bool g_emoteCanBreakOut = true;
	bool g_emoteNoEarlyOut = true;
	bool g_emoteIgnoreInvalid = true;
	bool g_emoteDestroyProps = true;

	void PlayEmote(const EmoteName& emote)
	{
		TASK::TASK_PLAY_EMOTE_WITH_HASH(Me(), emote.type, g_emotePlayback, GameUtil::Joaat(emote.name),
			g_emoteSecondary, g_emoteCanBreakOut, g_emoteNoEarlyOut, g_emoteIgnoreInvalid, g_emoteDestroyProps);
	}

	// KIT_EMOTE_ACTION_BLOW_KISS_1 -> "Blow Kiss".
	std::string EmoteLabel(const char* name)
	{
		std::string s = name;
		for (const char* prefix : { "KIT_EMOTE_REACTION_", "KIT_EMOTE_ACTION_", "KIT_EMOTE_TAUNT_", "KIT_EMOTE_GREET_", "KIT_EMOTE_DANCE_", "KIT_EMOTE_" })
			if (s.rfind(prefix, 0) == 0)
			{
				s.erase(0, strlen(prefix));
				break;
			}
		if (s.size() > 2 && s.compare(s.size() - 2, 2, "_1") == 0)
			s.resize(s.size() - 2);
		bool start = true;
		for (char& c : s)
		{
			if (c == '_')
			{
				c = ' ';
				start = true;
			}
			else
			{
				c = static_cast<char>(start ? std::toupper(static_cast<unsigned char>(c)) : std::tolower(static_cast<unsigned char>(c)));
				start = false;
			}
		}
		return s;
	}

	// Rampage's Smoke rows: a scenario started where the player stands.
	void StartScenarioHere(const char* scenario)
	{
		const Ped ped = Me();
		const Vector3 p = ENTITY::GET_ENTITY_COORDS(ped, TRUE, FALSE);
		TASK::TASK_START_SCENARIO_AT_POSITION(ped, GameUtil::Joaat(scenario), p.x, p.y, p.z, ENTITY::GET_ENTITY_HEADING(ped),
			-1, FALSE, FALSE, "", 0.0f, FALSE);
	}

	// --- Speech ---------------------------------------------------------

	bool g_speechDisplayAll = false;

	// PLAY_PED_AMBIENT_SPEECH_NATIVE's params struct, eight 8-byte script
	// slots: line, voice, variation, params hash, listener, sync over
	// network, two flags. Rampage's values; the params hash is
	// joaat("speech_params_force_normal").
	bool PlaySpeech(const std::string& line)
	{
		alignas(8) UINT64 params[8] = { reinterpret_cast<UINT64>(line.c_str()), 0, 0, 0x5BDE1482, 0, 1, 1, 1 };
		return AUDIO::PLAY_PED_AMBIENT_SPEECH_NATIVE(Me(), reinterpret_cast<Any*>(params)) != FALSE;
	}

	// The lines the player's current voice has, unless Display All is on.
	void AddSpeechRows(MenuBase* m, const std::vector<std::string>& lines)
	{
		const Ped ped = Me();
		size_t shown = 0;
		for (const std::string& line : lines)
		{
			if (!g_speechDisplayAll && !AUDIO::DOES_CONTEXT_EXIST_FOR_THIS_PED(ped, line.c_str(), FALSE))
				continue;
			Ui::Do(m, line, [line] { PlaySpeech(line); });
			shown++;
		}
		if (shown == 0)
			Ui::Section(m, lines.empty() ? "List is empty" : "No lines for this voice (try Display All)");
	}

	// A list the user supplies as a text file next to Rampagio.json.
	void FileSpeechList(MenuBase* parent, const std::string& title, const std::wstring& file, const std::string& fileName)
	{
		Ui::ListMenu(parent, title, [file, fileName](MenuBase* m)
		{
			const auto lines = DataFile::LoadLines(file);
			if (lines.empty())
			{
				Ui::Section(m, "Add lines to " + fileName);
				return;
			}
			AddSpeechRows(m, lines);
		});
	}

	// Rampage's Lenny rows: a scripted conversation from Saloon 1
	// (sal1 lines) with the player as ARTHUR.
	void PlayConversation(const char* root)
	{
		if (!AUDIO::_IS_SCRIPTED_CONVERSATION_CREATED(root))
			AUDIO::CREATE_NEW_SCRIPTED_CONVERSATION(root);
		AUDIO::ADD_PED_TO_CONVERSATION(root, Me(), "ARTHUR");
		AUDIO::_CLEAR_CONVERSATION_HISTORY_FOR_SCRIPTED_CONVERSATION(root);
		AUDIO::START_SCRIPT_CONVERSATION(root, TRUE, TRUE, FALSE);
	}

	void SetVoice(const std::string& voice) { AUDIO::SET_AMBIENT_VOICE_NAME(Me(), voice.c_str()); }

	// Rampage loads Arthur's six voice banks first.
	void SetVoiceArthur()
	{
		for (const char* bank : { "ARTHUR_NORMAL_01", "ARTHUR_NORMAL_02", "ARTHUR_NORMAL_03", "ARTHUR_NORMAL_04", "ARTHUR_NORMAL_05", "ARTHUR_NORMAL_06" })
			AUDIO::REQUEST_SCRIPT_AUDIO_BANK(bank);
		SetVoice("ARTHUR");
	}

	// --- Builders -------------------------------------------------------

	void BuildAnimations(MenuBase* self)
	{
		MenuBase* anims = Menus::Shared().animations = Ui::Submenu(self, "Animations");

		// SubSelfAnimationsCustom.
		MenuBase* custom = Ui::Submenu(anims, "Custom Animations");
		Ui::Text(custom, "player.customanimations.dictionary", "Dictionary", &g_animDict);
		Ui::Text(custom, "player.customanimations.animationname", "Animation Name", &g_animName);
		AddFlagRows(custom);
		Ui::Action(custom, "player.play", "Play", [] { return PlayAnim(g_animDict, g_animName); });
		Ui::Do(custom, "player.stop", "Stop", StopAnim);

		// SubSelfAnimationsDicts. Rampage reads PedAnimList.txt on "Reload
		// List"; ours has the script list built in and re-reads
		// Rampagio_PedAnimList.txt whenever the list opens.
		g_dictMenu = Ui::DetachedListMenu("Animation Dictionary", BuildDictMenu);
		MenuBase* dicts = Ui::Submenu(anims, "Dictionaries");
		Ui::ListMenu(dicts, "Search", [](MenuBase* m)
		{
			std::string text;
			if (!GameUtil::PromptText("Search:", text) || text.empty())
				return;
			text = Lower(text);
			for (const auto& [dict, list] : AnimDictionaries())
				if (dict.find(text) != std::string::npos)
					AddDictRow(m, dict, list.size());
			if (m->GetItemCount() == 0)
				Ui::Section(m, "No matches");
		});
		Ui::ListMenu(dicts, "All Dictionaries", [](MenuBase* m)
		{
			for (const auto& [dict, list] : AnimDictionaries())
				AddDictRow(m, dict, list.size());
		});

		// SubSelfFacialAnimations.
		MenuBase* facial = Ui::Submenu(anims, "Facial Animations");
		static std::string facialDict, facialName;
		Ui::Text(facial, "player.facialanimations.dictionary", "Dictionary", &facialDict);
		Ui::Text(facial, "player.facialanimations.animationname", "Animation Name", &facialName);
		Ui::Do(facial, "player.setoverride", "Set Override", [] { PED::SET_FACIAL_IDLE_ANIM_OVERRIDE(Me(), facialName.c_str(), facialDict.c_str()); });
		Ui::Do(facial, "player.clear", "Clear", [] { PED::CLEAR_FACIAL_IDLE_ANIM_OVERRIDE(Me()); });

		Ui::Do(anims, "player.stopanimation", "Stop Animation", StopAnim);
	}

	// SubEffects. Rampage's 25 named presets are its own table; ours lists
	// the effects the scripts start with a literal asset, plus Custom.
	void BuildEffects(MenuBase* self)
	{
		MenuBase* fx = Menus::Shared().effects = Ui::Submenu(self, "Effects");
		Ui::Number(fx, "player.scale", "Scale", &g_effectScale, 0.1f, 10.0f, 0.1f);
		Ui::Looped(fx, "player.loop", "Loop", LoopEffectTick);
		Ui::Section(fx, "Custom");
		Ui::Text(fx, "player.asset", "Asset", &g_effectAsset);
		Ui::Text(fx, "player.effectname", "Effect Name", &g_effectName);
		Ui::Action(fx, "player.playeffect", "Play Effect", [] { return PlayEffect(g_effectAsset, g_effectName, g_effectScale); });
		Ui::Section(fx, "From the Game Scripts");
		for (const NamePair& e : kEffects)
		{
			const std::string asset = e.first, name = e.second;
			Ui::Action(fx, Ui::Id("player.effect", name), name, [asset, name]
			{
				g_effectAsset = asset;
				g_effectName = name;
				return PlayEffect(asset, name, g_effectScale);
			});
		}
	}

	// SubEmotes. Rampage shows one emote type at a time (an "Emote Type"
	// choice); ours lists every type under its own heading, and adds the
	// gun twirls.
	void BuildEmotes(MenuBase* self)
	{
		MenuBase* emotes = Menus::Shared().emotes = Ui::Submenu(self, "Emotes");
		Ui::Do(emotes, "player.smoke", "Smoke", [] { StartScenarioHere("WORLD_HUMAN_SMOKE"); });
		Ui::Do(emotes, "player.smokeacigar", "Smoke a Cigar", [] { StartScenarioHere("WORLD_HUMAN_SMOKE_CIGAR"); });
		Ui::Do(emotes, "player.stopaction", "Stop Action", [] { TASK::CLEAR_PED_TASKS(Me(), TRUE, TRUE); });
		Ui::Do(emotes, "player.emoteoutro", "Emote Outro", [] { TASK::_TASK_EMOTE_OUTRO(Me()); });

		Ui::Section(emotes, "Emote Settings");
		Ui::Choice(emotes, "player.playback", "Playback", { "Upper Body", "Upper Body Loop", "Full Body" }, &g_emotePlayback);
		auto flag = [emotes](const char* caption, bool* value)
		{
			Ui::Toggle(emotes, caption, [value](bool on) { *value = on; })->SetState(*value);
		};
		flag("Is Secondary Task", &g_emoteSecondary);
		flag("Can Break Out", &g_emoteCanBreakOut);
		flag("Disable Early Out Anim Tag", &g_emoteNoEarlyOut);
		flag("Ignore Invalid Main Task", &g_emoteIgnoreInvalid);
		flag("Destroy Props", &g_emoteDestroyProps);

		const char* const kTypes[] = { "Reactions", "Actions", "Taunts", "Greetings", "Gun Twirls", "Dances" };
		for (int type = 0; type < 6; type++)
		{
			Ui::Section(emotes, kTypes[type]);
			for (const EmoteName& emote : kEmotes)
				if (emote.type == type)
					Ui::Do(emotes, Ui::Id("player.emote", emote.name), EmoteLabel(emote.name), [&emote] { PlayEmote(emote); });
		}
	}

	void BuildSpeech(MenuBase* self)
	{
		MenuBase* speech = Menus::Shared().speech = Ui::Submenu(self, "Play Speech");

		// SubVoiceChanger. Ours: "Set Voice" lists the voices the scripts
		// use, with Custom Input for any other.
		MenuBase* voice = Menus::Shared().voice = Ui::Submenu(speech, "Voice Changer");
		Ui::Do(voice, "player.setvoicetoarthur", "Set Voice to Arthur", SetVoiceArthur);
		Ui::Do(voice, "player.setvoicetojohn", "Set Voice to John", [] { SetVoice("JOHN_PLAYER"); });
		Ui::NameList(voice, "Set Voice", Names(kVoices), SetVoice);

		// SubPlaySpeechRegular / Flowgreet / Vignettes / Custom. Rampage has a
		// Display All toggle in each list; ours is one toggle for all of them.
		Ui::Toggle(speech, "player.displayall", "Display All", [](bool on) { g_speechDisplayAll = on; });
		Ui::ListMenu(speech, "Regular Speeches", [](MenuBase* m)
		{
			AddSpeechRows(m, std::vector<std::string>(std::begin(kSpeeches), std::end(kSpeeches)));
		});
		FileSpeechList(speech, "Flow Greets", L"Rampagio_SpeechFlowGreets.txt", "Rampagio_SpeechFlowGreets.txt");
		FileSpeechList(speech, "Vignettes", L"Rampagio_SpeechVignettes.txt", "Rampagio_SpeechVignettes.txt");
		FileSpeechList(speech, "Custom Speeches", L"Rampagio_SpeechList.txt", "Rampagio_SpeechList.txt");
		Ui::Action(speech, "player.custominput", "Custom Input", []
		{
			std::string line;
			if (!GameUtil::PromptText("Enter Speech Line:", line) || line.empty())
				return std::string();
			return PlaySpeech(line) ? std::string() : std::string("The ped can't say that line");
		})->SetHotkeyable(false);

		Ui::Section(speech, "Lines");
		Ui::Do(speech, "player.speech.lenny", "Lenny!?", [] { PlayConversation("SAL1_WHERE_LENN"); });
		Ui::Do(speech, "player.speech.lennyquestion", "Lenny?", [] { PlayConversation("SAL1_WHERE_LEN2"); });
		Ui::Do(speech, "player.speech.lennyshout", "Lenny!!!", [] { PlayConversation("SAL1_WHERE_LEN3"); });
		Ui::Do(speech, "player.foundlenny", "Found Lenny!?!", [] { PlayConversation("SAL1_ISTHATYOU"); });
		Ui::Do(speech, "player.scream", "Scream", [] { AUDIO::PLAY_PAIN(Me(), 1, 0.0f, FALSE, FALSE); });
	}
}

namespace Menus
{
	void BuildPlayerAnimations(MenuBase* self) { BuildAnimations(self); }
	void BuildPlayerEffects(MenuBase* self) { BuildEffects(self); }
	void BuildPlayerEmotes(MenuBase* self) { BuildEmotes(self); }
	void BuildPlayerSpeech(MenuBase* self) { BuildSpeech(self); }
}

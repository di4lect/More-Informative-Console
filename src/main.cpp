// Vampire Eye Glow Fix
// Removes (or tones down) the emissive "glow" baked into eye shapes, on the fly, as actors load.
// No file edits: FaceGen heads, head part meshes and plugins are left untouched.

#include "PCH.h"

namespace
{
	// ---------------------------------------------------------------- settings

	struct Settings
	{
		bool  enabled{ true };
		int   mode{ 0 };            // 0 = vampires only, 1 = every glowing eye
		float emissiveMult{ 0.0f };  // 0 = no glow at all
		float color[3]{ 0.0f, 0.0f, 0.0f };
		bool  replaceColor{ false };  // only when emissiveMult > 0 and a colour is set
		float threshold{ 0.01f };     // eyes whose glow (max channel * mult) is below this are left alone
		bool  debugLog{ false };
	};

	Settings g_settings;

	std::string ReadIniString(const char* a_section, const char* a_key, const char* a_default, const std::filesystem::path& a_path)
	{
		char buffer[256]{};
		GetPrivateProfileStringA(a_section, a_key, a_default, buffer, sizeof(buffer), a_path.string().c_str());
		std::string value = buffer;
		// strip inline comments and whitespace
		if (auto pos = value.find(';'); pos != std::string::npos) {
			value.erase(pos);
		}
		while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
			value.pop_back();
		}
		while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
			value.erase(value.begin());
		}
		return value;
	}

	void LoadSettings()
	{
		const std::filesystem::path path = "Data/SKSE/Plugins/VampireEyeGlowFix.ini";

		g_settings.enabled = ReadIniString("General", "bEnabled", "1", path) != "0";
		g_settings.mode = std::clamp(std::atoi(ReadIniString("General", "iMode", "0", path).c_str()), 0, 1);
		g_settings.debugLog = ReadIniString("General", "bDebugLog", "0", path) != "0";

		g_settings.emissiveMult = std::max(0.0f, static_cast<float>(std::atof(ReadIniString("Glow", "fEmissiveMult", "0.0", path).c_str())));
		g_settings.threshold = std::max(0.0f, static_cast<float>(std::atof(ReadIniString("Glow", "fGlowThreshold", "0.01", path).c_str())));

		std::string color = ReadIniString("Glow", "sEmissiveColor", "", path);
		int r = 0, g = 0, b = 0;
		if (!color.empty() && sscanf_s(color.c_str(), "%d , %d , %d", &r, &g, &b) == 3) {
			g_settings.color[0] = std::clamp(r, 0, 255) / 255.0f;
			g_settings.color[1] = std::clamp(g, 0, 255) / 255.0f;
			g_settings.color[2] = std::clamp(b, 0, 255) / 255.0f;
			g_settings.replaceColor = true;
		}

		logger::info("Settings: enabled={} mode={} ({}) emissiveMult={} replaceColor={} ({:.3f},{:.3f},{:.3f}) threshold={}",
			g_settings.enabled, g_settings.mode, g_settings.mode == 0 ? "vampires only" : "every glowing eye",
			g_settings.emissiveMult, g_settings.replaceColor, g_settings.color[0], g_settings.color[1], g_settings.color[2], g_settings.threshold);
	}

	// ---------------------------------------------------------------- detection helpers

	bool ContainsNoCase(std::string_view a_haystack, std::string_view a_needle)
	{
		auto it = std::search(a_haystack.begin(), a_haystack.end(), a_needle.begin(), a_needle.end(),
			[](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
		return it != a_haystack.end();
	}

	bool IsVampireActor(RE::Actor* a_actor)
	{
		if (auto race = a_actor->GetRace(); race && race->HasKeywordString("Vampire")) {
			return true;
		}
		if (auto base = a_actor->GetActorBase(); base && base->HasKeywordString("Vampire")) {
			return true;
		}
		return false;
	}

	bool IsEyeShape(RE::BSGeometry* a_geometry, RE::BSLightingShaderProperty* a_shader)
	{
		if (a_shader->material && a_shader->material->GetFeature() == RE::BSShaderMaterial::Feature::kEye) {
			return true;
		}
		// Some replacer eye meshes don't use the eye shader type. Head part shapes in FaceGen are named after the
		// head part's editor ID (e.g. "FemaleEyesHumanVampire"), so fall back to the name.
		return !a_geometry->name.empty() && ContainsNoCase(a_geometry->name.c_str(), "eyes");
	}

	float GlowStrength(RE::BSLightingShaderProperty* a_shader)
	{
		if (!a_shader->emissiveColor) {
			return 0.0f;
		}
		const auto& c = *a_shader->emissiveColor;
		return std::max({ c.red, c.green, c.blue }) * a_shader->emissiveMult;
	}

	// ---------------------------------------------------------------- the fix

	void FixActorEyes(RE::Actor* a_actor)
	{
		if (!g_settings.enabled || !a_actor) {
			return;
		}

		const bool actorIsVampire = IsVampireActor(a_actor);

		for (bool firstPerson : { false, true }) {
			auto root = a_actor->Get3D(firstPerson);
			if (!root) {
				continue;
			}

			RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* a_geometry) -> RE::BSVisit::BSVisitControl {
				const auto effect = a_geometry->GetGeometryRuntimeData().properties[RE::BSGeometry::States::kEffect];
				auto shader = netimmerse_cast<RE::BSLightingShaderProperty*>(effect.get());
				if (!shader || !IsEyeShape(a_geometry, shader)) {
					return RE::BSVisit::BSVisitControl::kContinue;
				}

				const float glow = GlowStrength(shader);
				if (glow <= g_settings.threshold) {
					return RE::BSVisit::BSVisitControl::kContinue;  // not glowing (or already fixed)
				}

				// Vampires-only mode: the actor is a vampire, or the eye shape itself is a vampire eye
				// (that also catches cured NPCs such as Serana whose FaceGen still carries vampire eyes).
				const bool shapeIsVampire = !a_geometry->name.empty() && ContainsNoCase(a_geometry->name.c_str(), "vampire");
				if (g_settings.mode == 0 && !actorIsVampire && !shapeIsVampire) {
					return RE::BSVisit::BSVisitControl::kContinue;
				}

				if (g_settings.emissiveMult > 0.0f && g_settings.replaceColor && shader->emissiveColor) {
					shader->emissiveColor->red = g_settings.color[0];
					shader->emissiveColor->green = g_settings.color[1];
					shader->emissiveColor->blue = g_settings.color[2];
				} else if (g_settings.emissiveMult <= 0.0f && shader->emissiveColor) {
					shader->emissiveColor->red = 0.0f;
					shader->emissiveColor->green = 0.0f;
					shader->emissiveColor->blue = 0.0f;
				}
				shader->emissiveMult = g_settings.emissiveMult;

				if (g_settings.debugLog) {
					logger::info("Fixed eye glow on {:08X} '{}' shape '{}' (glow was {:.3f})",
						a_actor->GetFormID(), a_actor->GetName(), a_geometry->name.c_str(), glow);
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		}
	}

	// Run now and again on the next frame: some heads finish attaching after the 3D is first set.
	void FixActorEyesNowAndLater(RE::Actor* a_actor)
	{
		FixActorEyes(a_actor);

		RE::ActorHandle handle = a_actor->GetHandle();
		SKSE::GetTaskInterface()->AddTask([handle]() {
			if (auto actor = handle.get()) {
				FixActorEyes(actor.get());
			}
		});
	}

	// ---------------------------------------------------------------- hooks

	template <class T>
	struct Load3DHook
	{
		static RE::NiAVObject* thunk(T* a_this, bool a_backgroundLoading)
		{
			auto result = func(a_this, a_backgroundLoading);
			FixActorEyesNowAndLater(a_this);
			return result;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	template <class T>
	struct Set3DHook
	{
		static void thunk(T* a_this, RE::NiAVObject* a_object, bool a_queue3DTasks)
		{
			func(a_this, a_object, a_queue3DTasks);
			if (a_object) {
				FixActorEyesNowAndLater(a_this);
			}
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	template <class T, class Hook>
	void WriteVfunc(REL::VariantID a_vtable, std::size_t a_index)
	{
		REL::Relocation<std::uintptr_t> vtbl{ a_vtable };
		Hook::func = vtbl.write_vfunc(a_index, Hook::thunk);
	}

	// Belt and braces: the loaded event also fires for background-loaded actors.
	class LoadedEventSink : public RE::BSTEventSink<RE::TESObjectLoadedEvent>
	{
	public:
		static LoadedEventSink* GetSingleton()
		{
			static LoadedEventSink singleton;
			return &singleton;
		}

		RE::BSEventNotifyControl ProcessEvent(const RE::TESObjectLoadedEvent* a_event, RE::BSTEventSource<RE::TESObjectLoadedEvent>*) override
		{
			if (a_event && a_event->loaded) {
				if (auto actor = RE::TESForm::LookupByID<RE::Actor>(a_event->formID)) {
					FixActorEyesNowAndLater(actor);
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	void InstallHooks()
	{
		// TESObjectREFR vtable: 0x6A = Load3D, 0x6C = Set3D (same index on SE, AE and VR)
		WriteVfunc<RE::Character, Load3DHook<RE::Character>>(RE::VTABLE_Character[0], 0x6A);
		WriteVfunc<RE::Character, Set3DHook<RE::Character>>(RE::VTABLE_Character[0], 0x6C);
		WriteVfunc<RE::PlayerCharacter, Load3DHook<RE::PlayerCharacter>>(RE::VTABLE_PlayerCharacter[0], 0x6A);
		WriteVfunc<RE::PlayerCharacter, Set3DHook<RE::PlayerCharacter>>(RE::VTABLE_PlayerCharacter[0], 0x6C);
		logger::info("Hooks installed");
	}

	void OnMessage(SKSE::MessagingInterface::Message* a_message)
	{
		switch (a_message->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			if (auto holder = RE::ScriptEventSourceHolder::GetSingleton()) {
				holder->AddEventSink<RE::TESObjectLoadedEvent>(LoadedEventSink::GetSingleton());
			}
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			if (auto player = RE::PlayerCharacter::GetSingleton()) {
				FixActorEyesNowAndLater(player);
			}
			break;
		default:
			break;
		}
	}

	void SetupLog()
	{
		auto path = SKSE::log::log_directory();
		if (!path) {
			SKSE::stl::report_and_fail("Unable to find the SKSE log directory.");
		}
		*path /= "VampireEyeGlowFix.log";
		auto log = std::make_shared<spdlog::logger>("Global", std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
	}
}

SKSEPluginInfo(
	.Version = REL::Version{ 1, 0, 0, 0 },
	.Name = "VampireEyeGlowFix",
	.Author = "di4lect",
	.StructCompatibility = SKSE::StructCompatibility::Independent,
	.RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary);

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SetupLog();
	logger::info("VampireEyeGlowFix 1.0.0 loading");

	if (a_skse->IsEditor()) {
		return false;
	}

	SKSE::Init(a_skse);
	LoadSettings();

	if (!g_settings.enabled) {
		logger::info("Disabled in ini; not installing hooks");
		return true;
	}

	InstallHooks();
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	return true;
}

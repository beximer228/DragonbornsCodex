#include "Input.h"

#include "Hotkeys.h"
#include "Menu.h"
#include "Overlay.h"
#include "Settings.h"

#include <imgui.h>

namespace Input
{
	namespace
	{
		constexpr std::uint32_t kEscape = 0x01;
		constexpr std::uint32_t kWheelUp = 8;
		constexpr std::uint32_t kWheelDown = 9;

		std::atomic_bool g_open{ false };

		// Modifier keys held, for the layout-switch chords.
		bool g_alt = false, g_shift = false, g_ctrl = false;

		void TrackLayoutChord(std::uint32_t a_dik, bool a_down)
		{
			const bool alt = a_dik == 0x38 || a_dik == 0xB8;
			const bool shift = a_dik == 0x2A || a_dik == 0x36;
			const bool ctrl = a_dik == 0x1D || a_dik == 0x9D;
			if (a_down && ((shift && (g_alt || g_ctrl)) || ((alt || ctrl) && g_shift))) {
				Overlay::RequestNextKeyboardLayout();
			}
			if (alt) {
				g_alt = a_down;
			} else if (shift) {
				g_shift = a_down;
			} else if (ctrl) {
				g_ctrl = a_down;
			}
		}
		// Gamepad: the right stick moves the cursor every frame (Overlay asks for it), L3 + R3 toggles the menu.
		std::atomic<float> g_rightX{ 0.0f }, g_rightY{ 0.0f };
		bool               g_leftThumb = false, g_rightThumb = false;

		ImGuiKey ToImGuiGamepadKey(std::uint32_t a_id)
		{
			using Key = RE::BSWin32GamepadDevice::Key;
			switch (a_id) {
			case Key::kUp: return ImGuiKey_GamepadDpadUp;
			case Key::kDown: return ImGuiKey_GamepadDpadDown;
			case Key::kLeft: return ImGuiKey_GamepadDpadLeft;
			case Key::kRight: return ImGuiKey_GamepadDpadRight;
			case Key::kA: return ImGuiKey_GamepadFaceDown;
			case Key::kX: return ImGuiKey_GamepadFaceLeft;
			case Key::kY: return ImGuiKey_GamepadFaceUp;
			case Key::kStart: return ImGuiKey_GamepadStart;
			case Key::kBack: return ImGuiKey_GamepadBack;
			default: return ImGuiKey_None;
			}
		}
		std::uint32_t    g_savedEnabled{ 0 };
		std::uint32_t    g_savedStored{ 0 };

		ImGuiKey ToImGuiKey(std::uint32_t a_dik)
		{
			switch (a_dik) {
			case 0x01: return ImGuiKey_Escape;
			case 0x0E: return ImGuiKey_Backspace;
			case 0x0F: return ImGuiKey_Tab;
			case 0x1C: return ImGuiKey_Enter;
			case 0x9C: return ImGuiKey_KeypadEnter;
			case 0x1D: return ImGuiKey_LeftCtrl;
			case 0x9D: return ImGuiKey_RightCtrl;
			case 0x2A: return ImGuiKey_LeftShift;
			case 0x36: return ImGuiKey_RightShift;
			case 0x38: return ImGuiKey_LeftAlt;
			case 0xB8: return ImGuiKey_RightAlt;
			case 0x39: return ImGuiKey_Space;
			case 0xC7: return ImGuiKey_Home;
			case 0xCF: return ImGuiKey_End;
			case 0xC9: return ImGuiKey_PageUp;
			case 0xD1: return ImGuiKey_PageDown;
			case 0xC8: return ImGuiKey_UpArrow;
			case 0xD0: return ImGuiKey_DownArrow;
			case 0xCB: return ImGuiKey_LeftArrow;
			case 0xCD: return ImGuiKey_RightArrow;
			case 0xD2: return ImGuiKey_Insert;
			case 0xD3: return ImGuiKey_Delete;
			case 0x1E: return ImGuiKey_A;
			case 0x2E: return ImGuiKey_C;
			case 0x21: return ImGuiKey_F;
			case 0x2F: return ImGuiKey_V;
			case 0x2D: return ImGuiKey_X;
			case 0x15: return ImGuiKey_Y;
			case 0x2C: return ImGuiKey_Z;
			default: return ImGuiKey_None;
			}
		}

		void UpdateModifiers(ImGuiIO& a_io, ImGuiKey a_key, bool a_down)
		{
			switch (a_key) {
			case ImGuiKey_LeftCtrl:
			case ImGuiKey_RightCtrl:
				a_io.AddKeyEvent(ImGuiMod_Ctrl, a_down);
				break;
			case ImGuiKey_LeftShift:
			case ImGuiKey_RightShift:
				a_io.AddKeyEvent(ImGuiMod_Shift, a_down);
				break;
			case ImGuiKey_LeftAlt:
			case ImGuiKey_RightAlt:
				a_io.AddKeyEvent(ImGuiMod_Alt, a_down);
				break;
			default:
				break;
			}
		}

		bool CanOpen();

		void Gamepad(RE::ButtonEvent* a_button)
		{
			using Key = RE::BSWin32GamepadDevice::Key;
			const auto id = a_button->GetIDCode();
			const bool edge = a_button->IsDown() || a_button->IsUp();
			if (edge && (id == Key::kLeftThumb || id == Key::kRightThumb)) {
				(id == Key::kLeftThumb ? g_leftThumb : g_rightThumb) = a_button->IsDown();
				if (a_button->IsDown() && g_leftThumb && g_rightThumb && (IsMenuOpen() || CanOpen())) {
					SetMenuOpen(!IsMenuOpen());
				}
				return;
			}
			if (!IsMenuOpen()) {
				return;
			}
			if (a_button->IsDown()) {
				switch (id) {
				case Key::kB:
					SetMenuOpen(false);
					return;
				case Key::kLeftShoulder:
				case Key::kRightShoulder:
					{
						auto lock = Overlay::Lock();
						Menu::CyclePage(id == Key::kRightShoulder ? 1 : -1);
					}
					return;
				default:
					break;
				}
			}
			auto  lock = Overlay::Lock();
			auto& io = ImGui::GetIO();
			if (id == Key::kLeftTrigger || id == Key::kRightTrigger) {
				if (a_button->IsPressed()) {  // repeats every frame while held
					io.AddMouseWheelEvent(0.0f, id == Key::kLeftTrigger ? 0.35f : -0.35f);
				}
				return;
			}
			if (const auto key = ToImGuiGamepadKey(id); key != ImGuiKey_None && edge) {
				io.AddKeyEvent(key, a_button->IsDown());
			}
		}

		void Thumbstick(RE::ThumbstickEvent* a_stick)
		{
			if (a_stick->IsRight()) {
				g_rightX = a_stick->xValue;
				g_rightY = a_stick->yValue;
				return;
			}
			if (!IsMenuOpen()) {
				return;
			}
			auto  lock = Overlay::Lock();
			auto& io = ImGui::GetIO();
			const float x = a_stick->xValue, y = a_stick->yValue;
			io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickLeft, x < -0.3f, std::max(0.0f, -x));
			io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickRight, x > 0.3f, std::max(0.0f, x));
			io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickUp, y > 0.3f, std::max(0.0f, y));
			io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickDown, y < -0.3f, std::max(0.0f, -y));
		}

		bool CanOpen()
		{
			auto ui = RE::UI::GetSingleton();
			auto player = RE::PlayerCharacter::GetSingleton();
			return Overlay::IsReady() && ui && player && player->Is3DLoaded() &&
			       !ui->IsMenuOpen(RE::MainMenu::MENU_NAME) &&
			       !ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME) &&
			       !ui->IsMenuOpen(RE::Console::MENU_NAME);
		}

		// One keyboard key edge: key binding capture, menu toggle, then (menu open) ImGui.
		std::array<std::atomic<bool>, 256> g_held{};  // keyboard keys held right now (DirectInput codes)

		void Keyboard(std::uint32_t a_id, bool a_isDown, bool a_isUp)
		{
			if (a_id < g_held.size() && (a_isDown || a_isUp)) {
				g_held[a_id] = a_isDown;
			}
			if (a_isDown && Hotkeys::OnKeyDown(a_id, IsMenuOpen())) {
				return;  // captured for a key binding
			}
			const auto toggleKey = Settings::Get().toggleKey;
			if (a_isDown && (a_id == toggleKey || (a_id == kEscape && IsMenuOpen()))) {
				const bool open = a_id == toggleKey ? !IsMenuOpen() : false;
				if (!open || CanOpen()) {
					SetMenuOpen(open);
				}
				return;
			}
			if (!IsMenuOpen() || !(a_isDown || a_isUp)) {
				return;
			}
			const bool down = a_isDown;
			auto       lock = Overlay::Lock();
			auto&      io = ImGui::GetIO();
			TrackLayoutChord(a_id, down);
			if (const auto key = ToImGuiKey(a_id); key != ImGuiKey_None) {
				UpdateModifiers(io, key, down);
				io.AddKeyEvent(key, down);
			}
		}

		class Sink final : public RE::BSTEventSink<RE::InputEvent*>
		{
		public:
			static Sink* GetSingleton()
			{
				static Sink singleton;
				return &singleton;
			}

			RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>*) override
			{
				for (auto event = a_event ? *a_event : nullptr; event; event = event->next) {
					Handle(event);
				}
				return RE::BSEventNotifyControl::kContinue;
			}

		private:
			void Handle(RE::InputEvent* a_event)
			{
				if (auto button = a_event->AsButtonEvent()) {
					const auto id = button->GetIDCode();
					if (button->GetDevice() == RE::INPUT_DEVICE::kKeyboard) {
						Keyboard(id, button->IsDown(), button->IsUp());
						return;
					}
					if (button->GetDevice() == RE::INPUT_DEVICE::kGamepad) {
						Gamepad(button);
						return;
					}
					if (!IsMenuOpen() || !(button->IsDown() || button->IsUp())) {
						return;
					}

					const bool down = button->IsPressed();
					auto       lock = Overlay::Lock();
					auto&      io = ImGui::GetIO();
					if (button->GetDevice() == RE::INPUT_DEVICE::kMouse) {
						if (id == kWheelUp || id == kWheelDown) {
							if (down) {
								io.AddMouseWheelEvent(0.0f, id == kWheelUp ? 1.0f : -1.0f);
							}
						} else if (id < 5) {
							io.AddMouseButtonEvent(static_cast<int>(id), down);
						}
					}
					return;
				}

				if (a_event->GetEventType() == RE::INPUT_EVENT_TYPE::kThumbstick) {
					Thumbstick(static_cast<RE::ThumbstickEvent*>(a_event));
					return;
				}
				if (!IsMenuOpen()) {
					return;
				}

				if (auto move = a_event->AsMouseMoveEvent()) {
					auto lock = Overlay::Lock();
					Overlay::MoveCursor(static_cast<float>(move->mouseInputX), static_cast<float>(move->mouseInputY));
				} else if (auto ch = a_event->AsCharEvent(); ch && !Overlay::HasNativeText()) {
					auto lock = Overlay::Lock();
					if (ch->keyCode >= 0x20 && ch->keyCode != 0x7F) {
						ImGui::GetIO().AddInputCharacter(ch->keyCode);
					}
				}
			}
		};
	}

	namespace
	{
		// The game's input dispatch (BSInputDeviceManager polls the devices, then sends the events to every sink,
		// the game's own handlers and ours). While the menu is open the game gets an empty list and the menu
		// handles the events itself, so the game's control states are never touched. Toggling them used to
		// leave the player stuck: a dialogue started while the menu was open saved "everything disabled" and
		// put that back when it ended.
		struct DispatchHook
		{
			static void Thunk(RE::BSTEventSource<RE::InputEvent*>* a_source, RE::InputEvent* const* a_events)
			{
				if (g_open && a_events) {
					Sink::GetSingleton()->ProcessEvent(a_events, a_source);
					// Writable, not const: hooks further down the chain (SKSE Menu Framework) write into the list,
					// and a const array lands in read-only memory, so that write crashed the game.
					static RE::InputEvent* none[1];
					none[0] = nullptr;
					original(a_source, none);
					return;
				}
				original(a_source, a_events);
			}
			static inline void (*original)(RE::BSTEventSource<RE::InputEvent*>*, RE::InputEvent* const*) = nullptr;
		};
		bool g_dispatchHooked = false;
	}

	void Install()
	{
		const auto site = REL::Relocation<std::uintptr_t>{ RELOCATION_ID(67315, 68617) }.address() + 0x7B;
		if (*reinterpret_cast<const std::uint8_t*>(site) == 0xE8) {
			DispatchHook::original = reinterpret_cast<decltype(DispatchHook::original)>(SKSE::GetTrampoline().write_call<5>(site, DispatchHook::Thunk));
			g_dispatchHooked = true;
			logger::info("Input dispatch hooked at {:X}", site - REL::Module::get().base());
		} else {
			logger::warn("Input dispatch call not found at {:X}: the menu falls back to toggling the game's controls", site - REL::Module::get().base());
		}
		if (auto manager = RE::BSInputDeviceManager::GetSingleton()) {
			manager->AddEventSink(Sink::GetSingleton());
			logger::info("Input sink installed (key 0x{:02X} toggles the menu)", Settings::Get().toggleKey);
		}
	}

	bool IsMenuOpen() { return g_open; }

	void SimulateKey(std::uint32_t a_dik, bool a_down) { Keyboard(a_dik, a_down, !a_down); }

	void SetMenuOpen(bool a_open)
	{
		if (g_open == a_open) {
			return;
		}
		auto controls = RE::ControlMap::GetSingleton();
		if (a_open) {
			if (!g_dispatchHooked) {  // fallback: the game's input would otherwise reach the game
				controls->GetControlsState(g_savedEnabled, g_savedStored);
				controls->ToggleControls(RE::ControlMap::UEFlag::kAll, false, false);
			}
			Overlay::OnMenuOpened();
		} else {
			if (!g_dispatchHooked) {
				// Re-enable exactly what was enabled before opening. ToggleControls (unlike
				// SetControlsState) notifies listeners, so the HUD that hid itself comes back.
				controls->ToggleControls(static_cast<RE::ControlMap::UEFlag>(g_savedEnabled), true, false);
			}
			Overlay::OnMenuClosed();
		}
		g_open = a_open;
	}

	bool IsKeyHeld(std::uint32_t a_dik) { return a_dik < g_held.size() && g_held[a_dik]; }

	std::pair<float, float> RightStick() { return { g_rightX, g_rightY }; }
}

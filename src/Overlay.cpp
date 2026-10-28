#include "Overlay.h"

#include "Archery.h"
#include "Cheats.h"
#include "Esp.h"
#include "Hotkeys.h"
#include "Input.h"
#include "LockpickHud.h"
#include "Menu.h"
#include "Settings.h"
#include "TestBridge.h"
#include "Textures.h"

#include <d3d11.h>
#include <dxgi.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>

namespace Overlay
{
	namespace
	{
		using PresentFn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);

		constexpr std::size_t kPresentIndex = 8;

		PresentFn            g_originalPresent{ nullptr };
		IDXGISwapChain*      g_swapChain{ nullptr };
		ID3D11Device*        g_device{ nullptr };
		ID3D11DeviceContext* g_context{ nullptr };

		std::atomic_bool     g_ready{ false };
		std::recursive_mutex g_mutex;
		Fonts                g_fonts;
		float                g_autoScale{ 1.0f };  // from the resolution; fonts are baked at this size
		ImVec2               g_cursor{ 0.0f, 0.0f };
		ImVector<ImWchar>    g_glyphRanges;

		HWND             g_hwnd{ nullptr };
		WNDPROC          g_originalWndProc{ nullptr };
		std::atomic_bool g_nativeText{ false };

		// A pending layout switch; done a moment later on the render thread and skipped if
		// Windows has switched by itself in the meantime, so it never switches twice.
		std::atomic_bool                      g_layoutRequested{ false };
		std::chrono::steady_clock::time_point g_layoutRequestTime;
		HKL                                   g_layoutAtRequest{ nullptr };
		constexpr auto                        kLayoutSwitchDelay = 150ms;

		HKL CurrentLayout() { return g_hwnd ? GetKeyboardLayout(GetWindowThreadProcessId(g_hwnd, nullptr)) : nullptr; }

		void ProcessLayoutRequest()
		{
			if (!g_layoutRequested || std::chrono::steady_clock::now() - g_layoutRequestTime < kLayoutSwitchDelay) {
				return;
			}
			g_layoutRequested = false;
			if (CurrentLayout() == g_layoutAtRequest) {
				ActivateKeyboardLayout(reinterpret_cast<HKL>(HKL_NEXT), KLF_SETFORPROCESS);
			}
		}

		// Text comes from WM_CHAR: Windows has already applied the active keyboard layout
		// (RU/EN/...), which the game's DirectInput stream does not track reliably.
		// Installed with SetWindowLongPtrW, so we receive UTF-16 even if the game window is ANSI.
		LRESULT CALLBACK HookedWndProc(HWND a_hwnd, UINT a_msg, WPARAM a_wParam, LPARAM a_lParam)
		{
			if (Input::IsMenuOpen()) {
				switch (a_msg) {
				case WM_CHAR:
					g_nativeText = true;
					if (a_wParam >= 0x20 && a_wParam != 0x7F) {
						auto lock = Lock();
						ImGui::GetIO().AddInputCharacterUTF16(static_cast<ImWchar16>(a_wParam));
					}
					return 0;
				case WM_INPUTLANGCHANGEREQUEST:
					// Alt+Shift / Ctrl+Shift / Win+Space: let Windows actually switch the layout.
					return DefWindowProcW(a_hwnd, a_msg, a_wParam, a_lParam);
				default:
					break;
				}
			}
			return CallWindowProcW(g_originalWndProc, a_hwnd, a_msg, a_wParam, a_lParam);
		}

		ImVec2 BackBufferSize()
		{
			DXGI_SWAP_CHAIN_DESC desc{};
			g_swapChain->GetDesc(&desc);
			return { static_cast<float>(desc.BufferDesc.Width), static_cast<float>(desc.BufferDesc.Height) };
		}

		const std::filesystem::path kOwnDir{ "Data/SKSE/Plugins/DragonbornsCodex" };
		const std::filesystem::path kWinFonts{ "C:/Windows/Fonts" };

		// A font path from DragonbornsCodex.ini: UTF-8, absolute or relative to our folder.
		std::filesystem::path FromSetting(const std::string& a_path)
		{
			std::filesystem::path path{ std::u8string{ a_path.begin(), a_path.end() } };
			return path.is_absolute() ? path : kOwnDir / path;
		}

		// The first existing candidate, at its size before scaling (ImGui 1.92 bakes glyphs at whatever size is
		// pushed, so a new resolution or menu size needs no atlas rebuild). Segoe UI and its symbol font are merged
		// in behind it for glyphs the font lacks (arrows, stars, ◆).
		ImFont* LoadFont(const std::vector<std::filesystem::path>& a_candidates, float a_size)
		{
			auto& io = ImGui::GetIO();
			for (const auto& path : a_candidates) {
				if (!std::filesystem::exists(path)) {
					continue;
				}
				ImFontConfig config;
				config.OversampleH = 2;
				config.OversampleV = 2;
				auto font = io.Fonts->AddFontFromFileTTF(path.string().c_str(), a_size, &config, g_glyphRanges.Data);
				if (!font) {
					continue;
				}
				for (const auto& fallback : { kWinFonts / "segoeui.ttf", kWinFonts / "seguisym.ttf" }) {
					if (fallback != path && std::filesystem::exists(fallback)) {
						ImFontConfig merge;
						merge.MergeMode = true;
						io.Fonts->AddFontFromFileTTF(fallback.string().c_str(), a_size, &merge, g_glyphRanges.Data);
					}
				}
				logger::info("Font {} ({:.0f}px)", path.string(), a_size);
				return font;
			}
			return nullptr;
		}

		void LoadFonts()
		{
			auto&       io = ImGui::GetIO();
			const auto& settings = Settings::Get();

			ImFontGlyphRangesBuilder builder;
			builder.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
			static constexpr ImWchar kExtra[] = { 0x2010, 0x2027, 0x2030, 0x203A, 0x2116, 0x2116, 0x2190, 0x21BB, 0x2605, 0x2606, 0x25C6, 0x25C6, 0 };
			builder.AddRanges(kExtra);
			builder.BuildRanges(&g_glyphRanges);

			const auto body = FromSetting(settings.fontBody);
			const auto heading = FromSetting(settings.fontHeading);
			const auto title = FromSetting(settings.fontTitle);
			g_fonts.body = LoadFont({ body, kOwnDir / "body.ttf", kWinFonts / "segoeui.ttf", kWinFonts / "arial.ttf" }, settings.fontBodySize);
			g_fonts.caption = LoadFont({ body, kOwnDir / "body.ttf", kWinFonts / "segoeui.ttf", kWinFonts / "arial.ttf" }, std::round(settings.fontBodySize * 0.8f));
			g_fonts.heading = LoadFont({ heading, kOwnDir / "heading.ttf", kWinFonts / "georgiab.ttf", kWinFonts / "segoeuib.ttf" }, settings.fontHeadingSize);
			g_fonts.title = LoadFont({ title, heading, kOwnDir / "heading.ttf", kWinFonts / "georgiab.ttf", kWinFonts / "segoeuib.ttf" }, settings.fontTitleSize);

			if (!g_fonts.body) {
				g_fonts.body = io.Fonts->AddFontDefault();
			}
			for (auto font : { &g_fonts.caption, &g_fonts.heading, &g_fonts.title }) {
				if (!*font) {
					*font = g_fonts.body;
				}
			}
			io.FontDefault = g_fonts.body;
		}

		float AutoScale(ImVec2 a_size)
		{
			// By height, so 16:9 and 21:9 screens of the same height get the same menu size.
			return std::clamp(a_size.y / 1080.0f, 0.75f, 2.5f);
		}

		void Render()
		{
			TestBridge::Poll();
			Hotkeys::Process();
			ProcessLayoutRequest();
			Cheats::Update();
			Archery::Update();
			const bool menuOpen = Input::IsMenuOpen();
			const bool lockpicking = LockpickHud::IsActive();
			const bool toast = Menu::HasToast();
			const bool esp = Esp::IsActive();
			if (!menuOpen && !lockpicking && !toast && !esp && !TestBridge::NeedsFrame()) {
				return;
			}

			auto  lock = Lock();
			auto& io = ImGui::GetIO();
			io.MouseDrawCursor = menuOpen;

			static auto last = std::chrono::steady_clock::now();
			const auto  now = std::chrono::steady_clock::now();
			io.DeltaTime = std::max(std::chrono::duration<float>(now - last).count(), 1.0f / 1000.0f);
			last = now;
			if (menuOpen) {
				// Gamepad right stick drives the cursor, faster the further it is pushed.
				const auto [x, y] = Input::RightStick();
				if (std::abs(x) > 0.15f || std::abs(y) > 0.15f) {
					const float speed = 1100.0f * g_autoScale * io.DeltaTime;
					MoveCursor(x * std::abs(x) * speed, -y * std::abs(y) * speed);
				}
			}
			io.DisplaySize = BackBufferSize();
			// A new resolution (or window size): sizes in the style are in pixels, so they are redone.
			if (const float scale = AutoScale(io.DisplaySize); scale != g_autoScale) {
				g_autoScale = scale;
				Menu::ApplyStyle();
			}

			ImGui_ImplDX11_NewFrame();
			ImGui::NewFrame();
			if (esp) {
				Esp::Draw();
			}
			if (menuOpen) {
				Menu::Draw();
			}
			if (lockpicking) {
				LockpickHud::Draw();
			}
			if (toast) {
				Menu::DrawToast();
			}
			ImGui::Render();

			// Created per frame so we never hold a back-buffer reference across ResizeBuffers.
			ID3D11Texture2D* backBuffer = nullptr;
			if (FAILED(g_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer)))) {
				return;
			}
			ID3D11RenderTargetView* rtv = nullptr;
			g_device->CreateRenderTargetView(backBuffer, nullptr, &rtv);
			backBuffer->Release();
			if (!rtv) {
				return;
			}

			ID3D11RenderTargetView* prevRTV = nullptr;
			ID3D11DepthStencilView* prevDSV = nullptr;
			g_context->OMGetRenderTargets(1, &prevRTV, &prevDSV);
			g_context->OMSetRenderTargets(1, &rtv, nullptr);

			ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
			TestBridge::AfterRender(g_swapChain, g_device, g_context);

			g_context->OMSetRenderTargets(1, &prevRTV, prevDSV);
			if (prevRTV) {
				prevRTV->Release();
			}
			if (prevDSV) {
				prevDSV->Release();
			}
			rtv->Release();
		}

		HRESULT WINAPI HookedPresent(IDXGISwapChain* a_swapChain, UINT a_syncInterval, UINT a_flags)
		{
			if (a_swapChain == g_swapChain) {
				Render();
			}
			return g_originalPresent(a_swapChain, a_syncInterval, a_flags);
		}
	}

	void Install()
	{
		auto window = RE::BSGraphics::Renderer::GetCurrentRenderWindow();
		auto data = RE::BSGraphics::Renderer::GetRendererDataSingleton();
		if (!window || !window->swapChain || !data) {
			logger::error("Overlay: renderer is not ready");
			return;
		}

		g_swapChain = reinterpret_cast<IDXGISwapChain*>(window->swapChain);
		g_device = reinterpret_cast<ID3D11Device*>(RE::BSGraphics::Renderer::GetDevice());
		g_context = reinterpret_cast<ID3D11DeviceContext*>(data->context);
		if (!g_device || !g_context) {
			logger::error("Overlay: no D3D11 device/context");
			return;
		}

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		auto& io = ImGui::GetIO();
		io.IniFilename = nullptr;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
		io.MouseDrawCursor = true;
		io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors | ImGuiBackendFlags_HasGamepad;

		const auto size = BackBufferSize();
		io.DisplaySize = size;
		g_autoScale = AutoScale(size);
		g_cursor = { size.x * 0.5f, size.y * 0.5f };

		LoadFonts();
		Textures::Init(g_device);
		Menu::ApplyStyle();

		if (!ImGui_ImplDX11_Init(g_device, g_context)) {
			logger::error("Overlay: ImGui_ImplDX11_Init failed");
			return;
		}

		auto  vtable = *reinterpret_cast<void***>(g_swapChain);
		DWORD oldProtect = 0;
		VirtualProtect(&vtable[kPresentIndex], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect);
		g_originalPresent = reinterpret_cast<PresentFn>(vtable[kPresentIndex]);
		vtable[kPresentIndex] = reinterpret_cast<void*>(&HookedPresent);
		VirtualProtect(&vtable[kPresentIndex], sizeof(void*), oldProtect, &oldProtect);

		g_hwnd = reinterpret_cast<HWND>(window->hWnd);
		if (g_hwnd) {
			g_originalWndProc = reinterpret_cast<WNDPROC>(
				SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&HookedWndProc)));
		}

		g_ready = true;
		logger::info("Overlay installed ({}x{}, scale {:.2f}, text input via {})", size.x, size.y, Scale(),
			g_originalWndProc ? "WM_CHAR" : "game events");
	}

	bool HasNativeText() { return g_nativeText; }

	void RequestNextKeyboardLayout()
	{
		if (g_layoutRequested) {
			return;
		}
		g_layoutAtRequest = CurrentLayout();
		g_layoutRequestTime = std::chrono::steady_clock::now();
		g_layoutRequested = true;
	}

	std::string KeyboardLanguage()
	{
		if (!g_hwnd) {
			return {};
		}
		const auto layout = GetKeyboardLayout(GetWindowThreadProcessId(g_hwnd, nullptr));
		const auto langId = LOWORD(reinterpret_cast<std::uintptr_t>(layout));
		wchar_t    name[9]{};
		if (!GetLocaleInfoW(MAKELCID(langId, SORT_DEFAULT), LOCALE_SISO639LANGNAME, name, 9)) {
			return {};
		}
		std::string result;
		for (const wchar_t* c = name; *c; ++c) {
			result += static_cast<char>(std::toupper(static_cast<unsigned char>(*c)));
		}
		return result;
	}

	bool         IsReady() { return g_ready; }
	const Fonts& GetFonts() { return g_fonts; }
	float        Scale() { return g_autoScale * Settings::Get().uiScale; }
	float        UserScale() { return Settings::Get().uiScale; }

	std::unique_lock<std::recursive_mutex> Lock() { return std::unique_lock{ g_mutex }; }

	void MoveCursor(float a_dx, float a_dy)
	{
		const auto& io = ImGui::GetIO();
		g_cursor.x = std::clamp(g_cursor.x + a_dx, 0.0f, io.DisplaySize.x - 1.0f);
		g_cursor.y = std::clamp(g_cursor.y + a_dy, 0.0f, io.DisplaySize.y - 1.0f);
		ImGui::GetIO().AddMousePosEvent(g_cursor.x, g_cursor.y);
	}

	void SetCursor(float a_x, float a_y)
	{
		auto lock = Lock();
		g_cursor = { a_x, a_y };
		ImGui::GetIO().AddMousePosEvent(a_x, a_y);
	}

	void OnMenuOpened()
	{
		auto  lock = Lock();
		auto& io = ImGui::GetIO();
		io.AddFocusEvent(true);
		io.AddMousePosEvent(g_cursor.x, g_cursor.y);
		Menu::OnOpen();
	}

	void OnMenuClosed()
	{
		auto lock = Lock();
		// Drops held keys/buttons so nothing stays "pressed" on the next open.
		ImGui::GetIO().AddFocusEvent(false);
	}

	unsigned long long CreateAlphaTexture(const unsigned char* a_alpha, int a_width, int a_height)
	{
		if (!g_device) {
			return 0;
		}
		std::vector<std::uint32_t> pixels(static_cast<std::size_t>(a_width) * a_height);
		for (std::size_t i = 0; i < pixels.size(); ++i) {
			pixels[i] = 0x00FFFFFFu | (static_cast<std::uint32_t>(a_alpha[i]) << 24);
		}
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = a_width;
		desc.Height = a_height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		D3D11_SUBRESOURCE_DATA data{ pixels.data(), static_cast<UINT>(a_width * 4), 0 };
		ID3D11Texture2D* texture = nullptr;
		if (FAILED(g_device->CreateTexture2D(&desc, &data, &texture))) {
			return 0;
		}
		ID3D11ShaderResourceView* view = nullptr;
		D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
		viewDesc.Format = desc.Format;
		viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		viewDesc.Texture2D.MipLevels = 1;
		const auto hr = g_device->CreateShaderResourceView(texture, &viewDesc, &view);
		texture->Release();
		return SUCCEEDED(hr) ? reinterpret_cast<unsigned long long>(view) : 0;
	}
}

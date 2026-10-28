#include "LockpickHud.h"

#include "Cheats.h"
#include "Overlay.h"

#include <imgui.h>

// What the lockpicking menu keeps (checked in the 1.6.1170 code, RVA 938CA0; CommonLib's names are off):
//   pickAngle        -fPickMaxAngle..+fPickMaxAngle (±90), 0 = straight up, mouse right = more
//   unk0F8           centre of the sweet spot, same scale
//   sweetSpotAngle   full width of the sweet spot: within half of it the lock turns all the way
//   partialPickAngle beyond that edge the lock still turns, less and less, for this many more degrees
namespace LockpickHud
{
	namespace
	{
		constexpr float kPi = 3.14159265f;
		constexpr float kMaxAngle = 90.0f;

		constexpr ImU32 kShadow = IM_COL32(0, 0, 0, 160);
		constexpr ImU32 kTrack = IM_COL32(18, 14, 10, 170);
		constexpr ImU32 kGold = IM_COL32(201, 154, 74, 255);
		constexpr ImU32 kGoldDim = IM_COL32(201, 154, 74, 110);
		constexpr ImU32 kGreen = IM_COL32(96, 214, 112, 255);
		constexpr ImU32 kYellow = IM_COL32(232, 188, 64, 255);
		constexpr ImU32 kRed = IM_COL32(222, 82, 64, 255);
		constexpr ImU32 kText = IM_COL32(236, 226, 206, 255);
		constexpr ImU32 kTextDim = IM_COL32(236, 226, 206, 150);

		RE::LockpickingMenu* GetMenu()
		{
			auto ui = RE::UI::GetSingleton();
			if (!ui || !ui->IsMenuOpen(RE::LockpickingMenu::MENU_NAME)) {
				return nullptr;
			}
			return ui->GetMenu<RE::LockpickingMenu>().get();
		}

		// Screen angle of a pick angle: 0 = up, positive = clockwise (to the right).
		float ScreenAngle(float a_degrees)
		{
			return (std::clamp(a_degrees, -kMaxAngle, kMaxAngle) - 90.0f) * kPi / 180.0f;
		}

		ImVec2 OnArc(ImVec2 a_center, float a_radius, float a_degrees)
		{
			const float t = ScreenAngle(a_degrees);
			return { a_center.x + std::cos(t) * a_radius, a_center.y + std::sin(t) * a_radius };
		}

		void Arc(ImDrawList* a_dl, ImVec2 a_center, float a_radius, float a_from, float a_to, ImU32 a_col, float a_thick)
		{
			a_from = std::max(a_from, -kMaxAngle);
			a_to = std::min(a_to, kMaxAngle);
			if (a_to - a_from < 0.05f) {
				return;
			}
			const int segments = std::max(4, static_cast<int>((a_to - a_from) / 2.0f));
			a_dl->PathArcTo(a_center, a_radius, ScreenAngle(a_from), ScreenAngle(a_to), segments);
			a_dl->PathStroke(a_col, ImDrawFlags_None, a_thick);
		}

		ImU32 Mix(ImU32 a_a, ImU32 a_b, float a_t)
		{
			const auto c = [&](int a_shift) {
				const float x = static_cast<float>((a_a >> a_shift) & 0xFF), y = static_cast<float>((a_b >> a_shift) & 0xFF);
				return static_cast<ImU32>(x + (y - x) * std::clamp(a_t, 0.0f, 1.0f)) << a_shift;
			};
			return c(IM_COL32_R_SHIFT) | c(IM_COL32_G_SHIFT) | c(IM_COL32_B_SHIFT) | c(IM_COL32_A_SHIFT);
		}

		ImU32 Alpha(ImU32 a_col, float a_alpha)
		{
			return (a_col & ~IM_COL32_A_MASK) | (static_cast<ImU32>(255.0f * std::clamp(a_alpha, 0.0f, 1.0f)) << IM_COL32_A_SHIFT);
		}

		ImVec2 TextSize(ImFont* a_font, const char* a_text)
		{
			return a_font->CalcTextSizeA(a_font->LegacySize * Overlay::Scale(), FLT_MAX, 0.0f, a_text);
		}

		void Text(ImDrawList* a_dl, ImFont* a_font, ImVec2 a_pos, ImU32 a_col, const char* a_text)
		{
			const float size = a_font->LegacySize * Overlay::Scale();
			a_dl->AddText(a_font, size, { a_pos.x + 1, a_pos.y + 1 }, kShadow, a_text);
			a_dl->AddText(a_font, size, a_pos, a_col, a_text);
		}

		// How far the lock turns with the pick here (0..1), as the game computes it.
		float TurnFraction(float a_pick, float a_center, float a_width, float a_partial)
		{
			const float off = std::abs(a_pick - a_center), half = a_width * 0.5f;
			if (off <= half) {
				return 1.0f;
			}
			return a_partial > 0.0f && off <= half + a_partial ? 1.0f - (off - half) / a_partial : 0.0f;
		}

		// A filled arrow pointing left (-1) or right (+1).
		void Arrow(ImDrawList* a_dl, ImVec2 a_mid, float a_size, int a_dir, ImU32 a_col)
		{
			const float d = static_cast<float>(a_dir);
			a_dl->AddTriangleFilled({ a_mid.x + d * a_size * 0.6f, a_mid.y }, { a_mid.x - d * a_size * 0.4f, a_mid.y - a_size * 0.55f },
				{ a_mid.x - d * a_size * 0.4f, a_mid.y + a_size * 0.55f }, a_col);
		}
	}

	bool IsActive()
	{
		return Cheats::Get().lockpickHud && GetMenu() != nullptr;
	}

	void Draw()
	{
		auto menu = GetMenu();
		if (!menu) {
			return;
		}
		const auto& data = menu->GetRuntimeData();
		const float pick = data.pickAngle;
		const float center = data.unk0F8;
		const float width = std::max(data.sweetSpotAngle, 0.5f);
		const float partial = std::max(data.partialPickAngle, 0.0f);
		const float half = width * 0.5f;
		const float turn = TurnFraction(pick, center, width, partial);
		const bool  turning = data.lockAngle > 0.5f;

		const auto&  io = ImGui::GetIO();
		const float  s = Overlay::Scale();
		const float  h = io.DisplaySize.y;
		auto         dl = ImGui::GetForegroundDrawList();
		auto         fonts = Overlay::GetFonts();
		// The pick turns around the keyhole, which sits a little right of and above the screen centre.
		const ImVec2 pivot{ io.DisplaySize.x * 0.5f + h * 0.055f, h * 0.454f };
		const float  radius = h * 0.27f;
		const float  thick = 16.0f * s;

		// Track: the whole range of the pick, red where the lock does not move at all.
		Arc(dl, pivot, radius, -kMaxAngle, kMaxAngle, kTrack, thick + 6.0f * s);
		Arc(dl, pivot, radius, -kMaxAngle, kMaxAngle, Alpha(kRed, 0.28f), thick);

		// The zone where the lock turns part of the way: brighter toward the sweet spot.
		constexpr int kSteps = 12;
		for (int side = -1; side <= 1; side += 2) {
			for (int i = 0; i < kSteps; ++i) {
				const float a = half + partial * i / kSteps, b = half + partial * (i + 1) / kSteps;
				const float from = side < 0 ? center - b : center + a, to = side < 0 ? center - a : center + b;
				Arc(dl, pivot, radius, from, to, Alpha(Mix(kYellow, kRed, (i + 0.5f) / kSteps), 0.85f - 0.55f * i / kSteps), thick);
			}
		}

		// Sweet spot: glow, then the bright band.
		Arc(dl, pivot, radius, center - half - 1.0f, center + half + 1.0f, Alpha(kGreen, 0.25f), thick + 14.0f * s);
		Arc(dl, pivot, radius, center - half, center + half, kGreen, thick);

		// Thin gold rims and ticks every 15°.
		const float outer = radius + thick * 0.5f + 3.0f * s;
		Arc(dl, pivot, radius - thick * 0.5f - 3.0f * s, -kMaxAngle, kMaxAngle, kGoldDim, 1.0f * s);
		Arc(dl, pivot, outer, -kMaxAngle, kMaxAngle, kGold, 1.5f * s);
		for (int deg = -90; deg <= 90; deg += 15) {
			const float len = (deg % 45 == 0 ? 10.0f : 5.0f) * s;
			dl->AddLine(OnArc(pivot, outer, static_cast<float>(deg)), OnArc(pivot, outer + len, static_cast<float>(deg)), kGoldDim, 1.5f * s);
		}

		// The pick: a pointer outside the arc aimed at the track, coloured by how far the lock will turn.
		const ImU32 pickCol = turn >= 1.0f ? kGreen : turn > 0.0f ? Mix(kRed, kYellow, turn) : kRed;
		{
			const float  t = ScreenAngle(pick);
			const ImVec2 dir{ std::cos(t), std::sin(t) }, side{ -dir.y, dir.x };
			const float  tip = outer + 2.0f * s, base = tip + 24.0f * s, w = 11.0f * s;
			const ImVec2 a{ pivot.x + dir.x * tip, pivot.y + dir.y * tip };
			const ImVec2 b{ pivot.x + dir.x * base + side.x * w, pivot.y + dir.y * base + side.y * w };
			const ImVec2 c{ pivot.x + dir.x * base - side.x * w, pivot.y + dir.y * base - side.y * w };
			dl->AddTriangleFilled({ a.x + 1, a.y + 2 }, { b.x + 1, b.y + 2 }, { c.x + 1, c.y + 2 }, kShadow);
			dl->AddTriangleFilled(a, b, c, pickCol);
			dl->AddTriangle(a, b, c, IM_COL32(255, 255, 255, 200), 1.5f * s);
			// and a bright mark across the track itself
			dl->AddLine(OnArc(pivot, radius - thick * 0.5f, pick), OnArc(pivot, radius + thick * 0.5f, pick), IM_COL32(255, 255, 255, 235), 3.0f * s);
		}

		// Panel under the lock: what to do, and how far the lock will turn right now.
		const float diff = center - pick;
		std::string hint;
		ImU32       hintCol = kText;
		int         arrow = 0;
		if (turning && turn < 1.0f) {
			hint = Tr("lock.release_the_lock_the_pick");
			hintCol = kRed;
		} else if (turn >= 1.0f) {
			hint = Tr("lock.here_turn_the_lock");
			hintCol = kGreen;
		} else {
			// Distance to the edge of the sweet spot, not its centre: that is how far the pick really has to go.
			const float need = std::max(std::abs(diff) - half, 1.0f);
			hint = Lang::Fmt("lock.by", diff > 0 ? Tr("lock.right") : Tr("lock.left"), std::round(need));
			hintCol = turn > 0.0f ? kYellow : kText;
			arrow = diff > 0 ? 1 : -1;
		}
		const std::string label = turn >= 1.0f ? std::string{ Tr("lock.the_lock_will_open") } :
		                          turn > 0.0f  ? Lang::Fmt("lock.the_lock_will_turn", turn * 100.0f) :
		                                         std::string{ Tr("lock.the_lock_will_not_turn") };

		const auto  hintSize = TextSize(fonts.heading, hint.c_str());
		const auto  labelSize = TextSize(fonts.caption, label.c_str());
		const float pad = 16.0f * s, barH = 6.0f * s;
		const float panelW = std::max(380.0f * s, hintSize.x + 2.0f * hintSize.y + 2.0f * pad);
		const float panelH = pad + hintSize.y + 10.0f * s + barH + 6.0f * s + labelSize.y + pad * 0.8f;
		const ImVec2 p0{ pivot.x - panelW * 0.5f, h * 0.755f }, p1{ p0.x + panelW, p0.y + panelH };
		dl->AddRectFilled({ p0.x + 2, p0.y + 3 }, { p1.x + 2, p1.y + 3 }, IM_COL32(0, 0, 0, 90), 8.0f * s);
		dl->AddRectFilled(p0, p1, IM_COL32(16, 13, 10, 215), 8.0f * s);
		dl->AddRect(p0, p1, kGoldDim, 8.0f * s, ImDrawFlags_None, 1.0f * s);

		const float arrowSize = hintSize.y * 0.8f, gap = 10.0f * s;
		const float rowW = hintSize.x + (arrow ? arrowSize + gap : 0.0f);
		float       x = pivot.x - rowW * 0.5f;
		const float y = p0.y + pad;
		if (arrow < 0) {
			Arrow(dl, { x + arrowSize * 0.5f, y + hintSize.y * 0.5f }, arrowSize, -1, hintCol);
			x += arrowSize + gap;
		}
		Text(dl, fonts.heading, { x, y }, hintCol, hint.c_str());
		if (arrow > 0) {
			Arrow(dl, { x + hintSize.x + gap + arrowSize * 0.5f, y + hintSize.y * 0.5f }, arrowSize, 1, hintCol);
		}

		// Meter: how far the lock turns with the pick where it is.
		const float barY = y + hintSize.y + 10.0f * s, barX0 = p0.x + pad, barX1 = p1.x - pad;
		dl->AddRectFilled({ barX0, barY }, { barX1, barY + barH }, IM_COL32(255, 255, 255, 25), barH);
		if (turn > 0.0f) {
			dl->AddRectFilled({ barX0, barY }, { barX0 + (barX1 - barX0) * turn, barY + barH }, pickCol, barH);
		}
		Text(dl, fonts.caption, { pivot.x - labelSize.x * 0.5f, barY + barH + 6.0f * s }, kTextDim, label.c_str());
	}
}

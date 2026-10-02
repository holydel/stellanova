#include "ui.h"

#include "resources.h"

#include <cmath>

namespace sn
{
using namespace ph;

void Ui::Begin(rhi::CommandList& commands, const render::FrameTime& time, Resources& from)
{
	resources = &from;
	scale = f32(commands.size.height) / HEIGHT;
	width = f32(commands.size.width) / scale;
	render::UpdateTextFonts();
	// One canvas unit is a pixel, with y up: the top left is (0, 0).
	render::Camera2D camera;
	camera.center = {0.5f * f32(commands.size.width), -0.5f * f32(commands.size.height)};
	camera.height = f32(commands.size.height);
	render::BeginCanvas(commands, time, camera);
}

void Ui::End(rhi::CommandList& commands) { render::EndCanvas(commands); }

Vec2 Ui::Measure(const char* text, const TextLook& look)
{
	text::LayoutDesc desc;
	desc.size = look.size * scale;
	desc.maxWidth = look.maxWidth * scale;
	text::LayoutText((look.bold ? resources->bold : resources->sans).Stack(), text, desc, layout);
	return {layout.size.x / scale, layout.size.y / scale};
}

Vec2 Ui::Text(const char* text, f32 x, f32 y, const TextLook& look)
{
	const Vec2 measured = Measure(text, look);
	if (look.align == Align::Center)
		x -= 0.5f * measured.x;
	else if (look.align == Align::Right)
		x -= measured.x;
	render::TextStyle style;
	style.color = look.color;
	style.glowWidth = look.glow;
	style.glowColor = look.glowColor;
	render::DrawText2D(layout, look.bold ? resources->bold : resources->sans,
	                   {x * scale, -y * scale}, style);
	return measured;
}

void Ui::Box(f32 x, f32 y, f32 w, f32 h, u32 color)
{
	const f32 halfW = 0.5f * w * scale;
	const f32 halfH = 0.5f * h * scale;
	render::DrawSprite({x * scale + halfW, -(y * scale + halfH)}, {halfW, halfH}, 0.0f, color);
}

void Ui::Line(Vec2 a, Vec2 b, f32 lineWidth, u32 color)
{
	// The canvas has y up; units have it down.
	const f32 dx = (b.x - a.x) * scale;
	const f32 dy = (a.y - b.y) * scale;
	const f32 length = std::sqrt(dx * dx + dy * dy);
	render::DrawSprite({0.5f * (a.x + b.x) * scale, -0.5f * (a.y + b.y) * scale},
	                   {0.5f * length, 0.5f * lineWidth * scale}, std::atan2(dy, dx), color);
}
} // namespace sn

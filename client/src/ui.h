#pragma once

#include <ph/render/canvas.h>
#include <ph/render/text.h>

// Drawing the interface on pith's canvas, in design units: the screen is 720
// units tall at any size, x grows to the right and y down from the top
// left. Text and boxes are drawn in order, over the scene.
namespace sn
{
struct Resources;

enum class Align : ph::u8
{
	Left,
	Center,
	Right,
};

struct TextLook
{
	ph::f32 size = 24.0f; // units per em
	ph::u32 color = 0xffffffff;
	bool bold = false;
	ph::f32 glow = 0.0f; // em
	ph::u32 glowColor = 0;
	Align align = Align::Left;
	ph::f32 maxWidth = 0.0f; // units; longer lines wrap (0: never)
};

class Ui
{
public:
	static constexpr ph::f32 HEIGHT = 720.0f;

	void Begin(ph::rhi::CommandList& commands, const ph::render::FrameTime& time,
	           Resources& resources);
	void End(ph::rhi::CommandList& commands);

	ph::f32 Width() const { return width; }
	ph::f32 PixelsPerUnit() const { return scale; }
	// A pointer's pixels in units.
	ph::Vec2 FromPixels(ph::f32 x, ph::f32 y) const { return {x / scale, y / scale}; }

	// Returns the text's size in units; `x` is where `align` puts it, `y` its
	// top.
	ph::Vec2 Text(const char* text, ph::f32 x, ph::f32 y, const TextLook& look);
	ph::Vec2 Measure(const char* text, const TextLook& look);
	void Box(ph::f32 x, ph::f32 y, ph::f32 w, ph::f32 h, ph::u32 color);
	// A line `width` units wide from `a` to `b`.
	void Line(ph::Vec2 a, ph::Vec2 b, ph::f32 width, ph::u32 color);

private:
	Resources* resources = nullptr;
	ph::f32 scale = 1.0f; // pixels per unit
	ph::f32 width = 0.0f;
	ph::text::TextLayout layout;
};
} // namespace sn

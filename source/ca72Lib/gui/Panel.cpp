#include "Panel.h"
#include "../dsp/freq_dial.h"

#include "BinaryData.h"

namespace ca72ui
{
namespace
{
// A Model D's charcoal (upstream measured #1d1b1a from a photograph), a little lighter here
// to allow for the shading laid over it.
const juce::Colour PANEL { 0xff282725 };
const juce::Colour INK { 0xffe9e6da };
const juce::Colour LINE { 0xffcfccc0 };
const juce::Colour BLUE { 0xff2f74c8 };
const juce::Colour RED { 0xffe0532a };
const juce::Colour IVORY { 0xffeae5d2 };
const juce::Colour LCD { 0xffff7a1a };

juce::Typeface::Ptr demi()
{
    static auto tf = juce::Typeface::createSystemTypefaceFor (BinaryData::URWGothicDemi_otf, BinaryData::URWGothicDemi_otfSize);
    return tf;
}

juce::Font gothic (float h) { return juce::Font (juce::FontOptions (demi()).withHeight (h)); }

float rad (float deg) { return juce::degreesToRadians (deg); }

// The panel's finish, pre-rendered once: fractal value noise (fine scale, six octaves, roughness
// 0.5) as a height map, bump-shaded by a light from the upper left. Highlights are white and
// shadows black, each with its own alpha, so the tile lays over any panel colour. Tileable.
juce::Image makePanelTexture()
{
    constexpr int N = 512, BASE = 128, OCTAVES = 6;
    constexpr float ROUGHNESS = 0.5f, STRENGTH = 3.0f, AMOUNT = 0.22f;
    juce::Random rnd (2024);
    std::vector<float> lattice ((size_t) N * N);
    for (auto& v : lattice)
        v = rnd.nextFloat();
    auto value = [&] (float x, float y, int cells)
    {
        // Periodic value noise on a cells x cells lattice, smoothstep-interpolated.
        const float fx = x * (float) cells / (float) N, fy = y * (float) cells / (float) N;
        const int x0 = (int) std::floor (fx), y0 = (int) std::floor (fy);
        const float tx = fx - (float) x0, ty = fy - (float) y0;
        const float sx = tx * tx * (3 - 2 * tx), sy = ty * ty * (3 - 2 * ty);
        auto at = [&] (int i, int j) { return lattice[(size_t) (((j % cells + cells) % cells) * N + ((i % cells + cells) % cells))]; };
        const float a = at (x0, y0), b = at (x0 + 1, y0), cc = at (x0, y0 + 1), d = at (x0 + 1, y0 + 1);
        return (a + (b - a) * sx) + ((cc + (d - cc) * sx) - (a + (b - a) * sx)) * sy;
    };
    std::vector<float> h ((size_t) N * N);
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x)
        {
            float sum = 0, amp = 1, norm = 0;
            for (int o = 0, cells = BASE; o < OCTAVES && cells <= N; ++o, cells *= 2, amp *= ROUGHNESS)
            {
                sum += amp * value ((float) x, (float) y, cells);
                norm += amp;
            }
            h[(size_t) (y * N + x)] = sum / norm;
        }
    juce::Image img (juce::Image::ARGB, N, N, true);
    juce::Image::BitmapData px (img, juce::Image::BitmapData::writeOnly);
    const float lx = -0.55f, ly = -0.65f, lz = 0.52f; // towards the light, upper left
    const float flat = lz / std::sqrt (lx * lx + ly * ly + lz * lz);
    auto H = [&] (int x, int y) { return h[(size_t) (((y + N) % N) * N + ((x + N) % N))]; };
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x)
        {
            const float dx = (H (x + 1, y) - H (x - 1, y)) * STRENGTH, dy = (H (x, y + 1) - H (x, y - 1)) * STRENGTH;
            const float nl = std::sqrt (dx * dx + dy * dy + 1.0f);
            const float shade = (-dx * lx - dy * ly + lz) / (nl * std::sqrt (lx * lx + ly * ly + lz * lz)) - flat;
            const float a = juce::jlimit (0.0f, 1.0f, std::abs (shade) * AMOUNT / 0.2f);
            px.setPixelColour (x, y, shade > 0 ? juce::Colours::white.withAlpha (a * 0.13f) : juce::Colours::black.withAlpha (a * 0.50f));
        }
    return img;
}

const juce::Image& panelTexture()
{
    static const juce::Image tex = makePanelTexture();
    return tex;
}

// A silk-screened line: cream paint with a little thickness (a shadow below, a catch of light
// above), the panel's grain showing through, edges slightly uneven and the odd worn patch.
void paintedLine (juce::Graphics& g, juce::Point<float> a, juce::Point<float> b, float width, float texel)
{
    const float len = a.getDistanceFrom (b);
    if (len <= 0.0f)
        return;
    // Leaves the caller's colour and fill as they were.
    juce::Graphics::ScopedSaveState keep (g);
    const auto along = (b - a) / len;
    const juce::Point<float> across (-along.y, along.x);
    juce::Random rnd ((juce::int64) (a.x * 7919.0f + a.y * 104729.0f + b.x * 31.0f + b.y));
    const float step = 5.0f;
    juce::Path paint;
    std::vector<std::pair<float, float>> wear; // (position, depth) of worn patches
    for (float t = 0; t < len; t += 40.0f + rnd.nextFloat() * 160.0f)
        if (rnd.nextFloat() < 0.5f)
            wear.push_back ({ t, 0.25f + rnd.nextFloat() * 0.45f });
    // The paint's outline, its width wandering by a fraction of a unit.
    std::vector<juce::Point<float>> left, right;
    for (float t = 0; t <= len + 0.01f; t += step)
    {
        const float tt = std::min (t, len);
        const float w = width * 0.5f * (0.93f + 0.14f * rnd.nextFloat());
        const auto p = a + along * tt;
        left.push_back (p + across * w);
        right.push_back (p - across * (w * (0.93f + 0.14f * rnd.nextFloat())));
    }
    paint.startNewSubPath (left.front());
    for (size_t i = 1; i < left.size(); ++i)
        paint.lineTo (left[i]);
    for (size_t i = right.size(); i-- > 0;)
        paint.lineTo (right[i]);
    paint.closeSubPath();

    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillPath (paint, juce::AffineTransform::translation (across.x * 0.0f + 0.8f, 1.2f));
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (paint);
        // Paint, slightly uneven in density along the line.
        for (float t = 0; t < len; t += step)
        {
            float alpha = 0.86f + 0.12f * rnd.nextFloat();
            for (auto [pos, depth] : wear)
            {
                const float d = std::abs (t - pos);
                if (d < 14.0f)
                    alpha *= 1.0f - depth * (1.0f - d / 14.0f);
            }
            const auto p0 = a + along * t, p1 = a + along * std::min (len, t + step + 0.5f);
            g.setColour (LINE.withAlpha (alpha));
            g.drawLine ({ p0, p1 }, width * 1.6f);
        }
        // The panel's grain through the paint, and a lit upper edge.
        juce::FillType grain (panelTexture(), juce::AffineTransform::scale (texel));
        grain.setOpacity (0.4f);
        g.setFillType (grain);
        g.fillPath (paint);
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.fillPath (paint, juce::AffineTransform::translation (-across.x * 0.6f - 0.4f, -0.8f));
    }
    // Scuffs: a few hairline scratches across the line.
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    for (auto [pos, depth] : wear)
        if (depth > 0.5f)
        {
            const auto c = a + along * pos;
            g.drawLine ({ c + across * width * 0.7f + along * 2.0f, c - across * width * 0.7f - along * 2.0f }, 0.6f);
        }
}

// Small lettering gets a soft dark outline and a slight drop shadow, so it reads on the grain.
void shadowed (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, juce::Justification j, float size, bool shadow)
{
    g.setFont (gothic (size));
    if (shadow && size <= 20.0f)
    {
        // Below a pixel the halo copies pile up under the glyphs and thicken them,
        // so keep the offsets at one device pixel and fade the halo instead
        const float pxPerUnit = g.getInternalContext().getPhysicalPixelScaleFactor();
        const float unit = std::max (1.1f, 1.0f / pxPerUnit);
        const float fade = juce::jlimit (0.4f, 1.0f, pxPerUnit * 1.1f);
        juce::Graphics::ScopedSaveState keep (g);
        g.setColour (juce::Colours::black.withAlpha (0.35f * fade));
        for (auto d : { juce::Point<float> (-1, 0), { 1, 0 }, { 0, -1 }, { 0, 1 }, { -0.7f, -0.7f }, { 0.7f, -0.7f }, { -0.7f, 0.7f }, { 0.7f, 0.7f } })
            g.drawText (t, r.translated (d.x * unit, d.y * unit), j, false);
        g.setColour (juce::Colours::black.withAlpha (0.6f * fade));
        g.drawText (t, r.translated (std::max (0.8f, 0.5f / pxPerUnit), std::max (1.9f, 1.0f / pxPerUnit)), j, false);
    }
    g.drawText (t, r, j, false);
}

// Text centred on (x, y) in drawing units.
void legend (juce::Graphics& g, float x, float y, const juce::String& t, float size = 19.0f, bool shadow = true)
{
    shadowed (g, t, juce::Rectangle<float> (x - 300.0f, y - size * 0.75f, 600.0f, size * 1.5f), juce::Justification::centred, size, shadow);
}

// "ON" with its last letter at a switch's right edge.
void on (juce::Graphics& g, float right, float y, float size = 19.0f, bool shadow = true)
{
    shadowed (g, "ON", juce::Rectangle<float> (right - 200.0f, y - size * 0.75f, 200.0f, size * 1.5f), juce::Justification::centredRight, size, shadow);
}

void tick (juce::Graphics& g, float cx, float cy, float deg, float r0, float r1, float w)
{
    const float a = rad (deg);
    g.drawLine (cx + r0 * std::sin (a), cy - r0 * std::cos (a), cx + r1 * std::sin (a), cy - r1 * std::cos (a), w);
}

void wavePath (juce::Graphics& g, float cx, float cy, int kind)
{
    // Upstream's pictograms (art.rs), 24 by 14 units.
    static const std::vector<std::vector<juce::Point<float>>> shapes {
        { { -11, 7 }, { 0, -7 }, { 11, 7 } },
        { { -11, 7 }, { 3, -7 }, { 3, 0 }, { 11, 7 } },
        { { -10, 7 }, { 10, -7 }, { 10, 7 } },
        { { -12, 7 }, { -12, -7 }, { 0, -7 }, { 0, 7 }, { 12, 7 } },
        { { -12, 7 }, { -12, -7 }, { -3, -7 }, { -3, 7 }, { 12, 7 } },
        { { -12, 7 }, { -12, -7 }, { -7, -7 }, { -7, 7 }, { 12, 7 } },
        { { -10, 7 }, { -10, -7 }, { 10, 7 } },
    };
    juce::Path p;
    const auto& s = shapes[(size_t) kind];
    p.startNewSubPath (cx + s[0].x, cy + s[0].y);
    for (size_t i = 1; i < s.size(); ++i)
        p.lineTo (cx + s[i].x, cy + s[i].y);
    g.strokePath (p, juce::PathStrokeType (3.0f));
}
} // namespace

struct Look : public juce::LookAndFeel_V4
{
    Look()
    {
        setColour (juce::TooltipWindow::backgroundColourId, juce::Colours::black);
        setColour (juce::TooltipWindow::textColourId, juce::Colours::white);
        setColour (juce::TooltipWindow::outlineColourId, juce::Colours::white);
        setColour (juce::BubbleComponent::backgroundColourId, juce::Colours::black);
        setColour (juce::BubbleComponent::outlineColourId, juce::Colours::white);
        // Not quite opaque, so the list's rounded corners show what is behind them.
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xfe000000));
        setColour (juce::PopupMenu::textColourId, juce::Colours::white);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff3a2a1c));
        setColour (juce::PopupMenu::highlightedTextColourId, LCD);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s) override
    {
        auto* k = dynamic_cast<Knob*> (&s);
        const bool big = k != nullptr && k->big;
        const bool sel = k != nullptr && k->isSelector;
        const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        const float r = std::min (b.getWidth(), b.getHeight()) * 0.5f;
        const auto c = b.getCentre();
        const float angle = start + pos * (end - start);


        // Black knurled skirt.
        juce::Path skirt;
        const int ridges = sel ? 20 : 36;
        for (int i = 0; i <= ridges * 2; ++i)
        {
            const float a = juce::MathConstants<float>::twoPi * (float) i / (float) (ridges * 2) + angle;
            const float rr = (i % 2 == 0) ? r : r * 0.955f;
            const auto pt = c.getPointOnCircumference (rr, a);
            i == 0 ? skirt.startNewSubPath (pt) : skirt.lineTo (pt);
        }
        skirt.closeSubPath();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3a3a), c.x - r, c.y - r, juce::Colour (0xff080808), c.x + r, c.y + r, false));
        g.fillPath (skirt);

        if (sel)
        {
            // Selector: a black pointer knob with a white line.
            g.setColour (juce::Colour (0xff141414));
            g.fillEllipse (juce::Rectangle<float> (r * 1.5f, r * 1.5f).withCentre (c));
            // Its white line inlaid: a shade above, a lit edge below.
            const juce::Line<float> line { c.getPointOnCircumference (r * 0.2f, angle), c.getPointOnCircumference (r * 0.9f, angle) };
            const float lw = r * 0.12f;
            g.setColour (juce::Colours::black.withAlpha (0.7f));
            g.drawLine (line.withShortenedStart (0).withShortenedEnd (0), lw * 1.5f);
            g.setColour (juce::Colours::white.withAlpha (0.25f));
            g.drawLine ({ line.getStart().translated (0, lw * 0.35f), line.getEnd().translated (0, lw * 0.35f) }, lw * 1.1f);
            g.setColour (INK);
            g.drawLine (line, lw);
            return;
        }

        // Aluminium cap: a conical brushed sheen that stays put while the knob turns (cached).
        const float cap = r * (big ? 0.754f : 0.667f);
        const auto capRect = juce::Rectangle<float> (cap * 2, cap * 2).withCentre (c);
        const float px = g.getInternalContext().getPhysicalPixelScaleFactor();
        g.drawImage (capImage ((int) std::ceil (cap * 2 * px)), capRect);
        // The pointer: a black line across the cap, ending in a dot near its edge.
        // It runs off the cap and is cut flush with the cap's rim.
        const auto dot = c.getPointOnCircumference (cap * 1.3f, angle);
        juce::Graphics::ScopedSaveState clipToCap (g);
        juce::Path capShape;
        capShape.addEllipse (capRect);
        g.reduceClipRegion (capShape);
        // A bold pill from the centre to the edge, rounded at both ends.
        // Engraved in the cap and filled with black paint, lit from straight above: the groove's
        // lower wall catches the light, its upper wall falls into shade, the paint has a soft gloss.
        juce::Path pointer;
        pointer.startNewSubPath (c);
        pointer.lineTo (dot);
        const float pw = r * (big ? 0.13f : 0.14f);
        auto stroke = [&] (float width, juce::Point<float> off, juce::Colour col)
        {
            g.setColour (col);
            g.strokePath (pointer, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                          juce::AffineTransform::translation (off));
        };
        stroke (pw * 1.45f, { 0.0f, pw * 0.22f }, juce::Colours::white.withAlpha (0.55f));
        stroke (pw * 1.35f, { 0.0f, -pw * 0.16f }, juce::Colours::black.withAlpha (0.45f));
        stroke (pw * 1.15f, {}, juce::Colour (0xff2a2a2a));
        stroke (pw, { 0.0f, pw * 0.08f }, juce::Colour (0xff0a0a0a));
        stroke (pw * 0.3f, { 0.0f, -pw * 0.18f }, juce::Colours::white.withAlpha (0.16f));
    }

    // The cap's conical brushing: several light lobes around the centre, fine radial streaks,
    // a darker bevel at the rim. One image per pixel size.
    juce::Image capImage (int size)
    {
        if (auto it = caps.find (size); it != caps.end())
            return it->second;
        juce::Image img (juce::Image::ARGB, size, size, true);
        juce::Image::BitmapData px (img, juce::Image::BitmapData::writeOnly);
        const float c = (float) size * 0.5f;
        juce::Random rnd (11);
        std::array<float, 720> streak {};
        for (auto& s : streak)
            s = rnd.nextFloat() - 0.5f;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                const float dx = (float) x + 0.5f - c, dy = (float) y + 0.5f - c;
                const float d = std::sqrt (dx * dx + dy * dy) / c;
                if (d > 1.0f)
                    continue;
                const float th = std::atan2 (dy, dx);
                float v = 0.80f
                          + 0.11f * std::cos (2.0f * (th + 0.9f))
                          + 0.06f * std::cos (3.0f * (th - 0.4f))
                          + 0.04f * std::cos (6.0f * (th + 0.2f));
                const int k = (int) ((th + juce::MathConstants<float>::pi) / juce::MathConstants<float>::twoPi * 720.0f) % 720;
                v += 0.035f * streak[(size_t) k] * std::min (1.0f, d * 3.0f);
                v -= 0.05f * (1.0f - d);
                if (d > 0.9f)
                    v -= (d - 0.9f) * 2.2f;
                v = juce::jlimit (0.0f, 1.0f, v);
                const float a = juce::jlimit (0.0f, 1.0f, (1.0f - d) * c);
                px.setPixelColour (x, y, juce::Colour::fromFloatRGBA (v, v, v * 0.985f, 1.0f).withMultipliedAlpha (a));
            }
        caps[size] = img;
        return img;
    }

    std::map<int, juce::Image> caps;

    static juce::Font sans (float h)
    {
        for (auto name : { "Helvetica", "Arial" })
            if (juce::Font::findAllTypefaceNames().contains (name))
                return juce::Font (juce::FontOptions (name, h, juce::Font::bold));
        return juce::Font (juce::FontOptions (h, juce::Font::bold));
    }

    juce::Font getSliderPopupFont (juce::Slider&) override { return sans (14.0f); }
    int getSliderPopupPlacement (juce::Slider&) override { return juce::BubbleComponent::above; }

    void drawBubble (juce::Graphics& g, juce::BubbleComponent&, const juce::Point<float>&, const juce::Rectangle<float>& body) override
    {
        const float r = body.getHeight() * 0.5f;
        g.setColour (juce::Colours::black);
        g.fillRoundedRectangle (body, r);
        g.setColour (juce::Colours::white);
        g.drawRoundedRectangle (body.reduced (0.75f), r - 0.75f, 1.5f);
    }

    void drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h) override
    {
        const auto body = juce::Rectangle<float> (0, 0, (float) w, (float) h);
        g.setColour (juce::Colours::black);
        g.fillRoundedRectangle (body, (float) h * 0.5f);
        g.setColour (juce::Colours::white);
        g.drawRoundedRectangle (body.reduced (0.75f), (float) h * 0.5f - 0.75f, 1.5f);
        g.setFont (sans (13.0f));
        g.drawText (text, 0, 0, w, h, juce::Justification::centred);
    }

    juce::Rectangle<int> getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent) override
    {
        const int w = juce::GlyphArrangement::getStringWidthInt (sans (13.0f), text) + 24, h = 22;
        return juce::Rectangle<int> (pos.x, pos.y + 18, w, h).constrainedWithin (parent);
    }

    // The preset display: an amber LCD behind glass in a dark bezel, its unlit dots faintly showing.
    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        const auto outer = juce::Rectangle<float> (0, 0, (float) w, (float) h);
        const float radius = (float) h * 0.12f;
        g.setColour (juce::Colour (0xff050505));
        g.fillRoundedRectangle (outer, radius);
        const auto glass = outer.reduced ((float) h * 0.09f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1c0d04), 0, glass.getY(), juce::Colour (0xff120802), 0, glass.getBottom(), false));
        g.fillRoundedRectangle (glass, radius * 0.6f);
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (glass.toNearestInt());
            const float step = std::max (2.0f, (float) h * 0.075f);
            g.setColour (LCD.withAlpha (0.07f));
            for (float y = glass.getY() + step * 0.5f; y < glass.getBottom(); y += step)
                for (float x = glass.getX() + step * 0.5f; x < glass.getRight(); x += step)
                    g.fillRect (x, y, step * 0.55f, step * 0.55f);
            // The glass: a top reflection and an inner shadow.
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.06f), 0, glass.getY(), juce::Colours::transparentWhite, 0, glass.getCentreY(), false));
            g.fillRect (glass);
            g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.6f), 0, glass.getY(), juce::Colours::transparentBlack, 0, glass.getY() + (float) h * 0.2f, false));
            g.fillRect (glass);
        }
        juce::Path arrow;
        const float s = (float) h * 0.14f;
        arrow.addTriangle ((float) w - s * 3.5f, (float) h * 0.5f - s * 0.5f, (float) w - s * 1.5f, (float) h * 0.5f - s * 0.5f, (float) w - s * 2.5f, (float) h * 0.5f + s * 0.6f);
        g.setColour (LCD.withAlpha (0.85f));
        g.fillPath (arrow);
        // The name, its letters' own outline centred on the glass both ways.
        const auto text = box.getSelectedId() > 0 ? box.getText() : box.getTextWhenNothingSelected();
        juce::GlyphArrangement ga;
        ga.addLineOfText (getComboBoxFont (box), text, 0.0f, 0.0f);
        // Glyph bounds are the font's ascent and descent; the outline gives the letters' real extent.
        juce::Path outline;
        ga.createPath (outline);
        const auto ink = outline.getBounds();
        ga.moveRangeOfGlyphs (0, -1, glass.getCentreX() - ink.getCentreX(), glass.getCentreY() - ink.getCentreY());
        g.setColour (LCD);
        ga.draw (g);
    }

    // The preset list: black, a white outline, rounded corners.
    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h);
        g.setColour (juce::Colours::black);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (juce::Colours::white);
        g.drawRoundedRectangle (r.reduced (0.75f), 8.0f, 1.5f);
    }

    int getPopupMenuBorderSize() override { return 6; }

    // The section titles' face (URW Gothic Demi).
    juce::Font getComboBoxFont (juce::ComboBox& b) override { return gothic ((float) b.getHeight() * 0.6f); }
    juce::Font getPopupMenuFont() override { return gothic (19.0f); }

    // The display draws its own name (drawComboBox); the label stays out of sight.
    void positionComboBoxText (juce::ComboBox&, juce::Label& l) override
    {
        l.setBounds ({});
        l.setColour (juce::Label::textColourId, juce::Colours::transparentBlack);
    }

    // A raised keycap in a dark bezel: side walls catching the light at the top, a graded
    // face; pressed, it sinks and the walls go.
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        const auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        const float radius = r.getHeight() * 0.18f;
        g.setColour (juce::Colour (0xff050505));
        g.fillRoundedRectangle (r, radius);
        auto cap = r.reduced (r.getHeight() * 0.06f);
        if (down)
            cap = cap.reduced (r.getHeight() * 0.03f).translated (0.0f, r.getHeight() * 0.03f);
        // The walls.
        g.setGradientFill (juce::ColourGradient (juce::Colour (down ? 0xff2a2a2a : 0xff6a6a68), 0, cap.getY(), juce::Colour (0xff1a1a1a), 0, cap.getBottom(), false));
        g.fillRoundedRectangle (cap, radius * 0.85f);
        // The face, set in from the walls more at the bottom than the top.
        const float inset = cap.getHeight() * (down ? 0.05f : 0.09f);
        auto face = cap.withTrimmedLeft (inset).withTrimmedRight (inset).withTrimmedTop (inset * 0.6f).withTrimmedBottom (inset * 1.6f);
        juce::ColourGradient top (juce::Colour (over && ! down ? 0xff4a4a48 : 0xff3c3c3a), 0, face.getY(), juce::Colour (0xff262625), 0, face.getBottom(), false);
        g.setGradientFill (top);
        g.fillRoundedRectangle (face, radius * 0.6f);
        g.setColour (juce::Colours::white.withAlpha (down ? 0.04f : 0.10f));
        g.drawLine (face.getX() + radius * 0.6f, face.getY() + 0.75f, face.getRight() - radius * 0.6f, face.getY() + 0.75f, 1.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down) override
    {
        const auto t = b.getButtonText();
        if (t != "<" && t != ">")
            return LookAndFeel_V4::drawButtonText (g, b, false, down);
        const auto c = b.getLocalBounds().toFloat().getCentre().translated (0.0f, down ? (float) b.getHeight() * 0.04f : 0.0f);
        const float s = (float) b.getHeight() * 0.16f;
        const float dir = t == "<" ? -1.0f : 1.0f;
        juce::Path chevron;
        chevron.startNewSubPath (c.x - dir * s * 0.5f, c.y - s);
        chevron.lineTo (c.x + dir * s * 0.5f, c.y);
        chevron.lineTo (c.x - dir * s * 0.5f, c.y + s);
        g.setColour (juce::Colours::white);
        g.strokePath (chevron, juce::PathStrokeType (s * 0.42f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return gothic ((float) h * 0.55f); }
};

Knob::Knob (bool bigKnob, bool selector) : juce::Slider (juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox), big (bigKnob), isSelector (selector)
{
    setPopupDisplayEnabled (true, true, nullptr);
    setMouseDragSensitivity (selector ? 120 : 220);
    setVelocityBasedMode (false);
}

double Knob::snapValue (double attempted, juce::Slider::DragMode)
{
    if (snap && juce::ModifierKeys::currentModifiers.isShiftDown())
        return snap (attempted);
    return attempted;
}

Rocker::Rocker (RockerColour c, bool v, float share) : colour (c), vertical (v), padShare (share) {}

juce::Rectangle<float> Rocker::body() const
{
    return getLocalBounds().toFloat().reduced ((float) getWidth() * padShare);
}

bool Rocker::hitTest (int x, int y) { return body().contains ((float) x, (float) y); }

void Rocker::paintButton (juce::Graphics& g, bool over, bool)
{
    auto r = body().reduced (2.0f);
    const bool on = getToggleState();
    const juce::Colour base = colour == RockerColour::Blue ? BLUE : colour == RockerColour::Red ? RED : IVORY;
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (r.expanded (1.5f), 3.0f);
    // The rocker's two halves: the pressed one darker and sunk.
    auto first = vertical ? r.removeFromTop (r.getHeight() * 0.5f) : r.removeFromLeft (r.getWidth() * 0.5f);
    auto second = r;
    // ON sits on the second half (right, or bottom when vertical): pressed in when on.
    auto& pressed = on ? second : first;
    auto& raised = on ? first : second;
    const juce::Colour face = base;
    g.setGradientFill (juce::ColourGradient (face.brighter (0.35f), raised.getX(), raised.getY(), face.darker (0.25f), raised.getRight(), raised.getBottom(), false));
    g.fillRect (raised);
    g.setColour (face.darker (0.55f));
    g.fillRect (pressed);
    g.setColour (juce::Colours::white.withAlpha (over ? 0.35f : 0.2f));
    g.drawRect (raised, 1.0f);
}

Toggle::Toggle (juce::RangedAudioParameter& p)
    : param (p), attachment (p, [this] (float v) { index = (int) std::lround (v); repaint(); })
{
    attachment.sendInitialUpdate();
}

void Toggle::paint (juce::Graphics& g)
{
    // A panel toggle seen from the front: a chrome nut around the bushing, and the bat leaning
    // left (LOW) or right (HIGH), or pointing straight out (LAST) so only its ball shows.
    const auto b = getLocalBounds().toFloat();
    const auto c = b.getCentre();
    const float r = std::min (b.getWidth(), b.getHeight()) * 0.22f;

    // The nut: a chrome ring with a dark hole.
    juce::ColourGradient ring (juce::Colour (0xfff2f2ee), c.x - r, c.y - r, juce::Colour (0xff5c5c58), c.x + r, c.y + r, false);
    g.setGradientFill (ring);
    g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
    g.setColour (juce::Colour (0xff2a2a28));
    g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c), r * 0.08f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff050505), c.x, c.y - r * 0.6f, juce::Colour (0xff2e2e2c), c.x, c.y + r * 0.6f, false));
    g.fillEllipse (juce::Rectangle<float> (r * 1.25f, r * 1.25f).withCentre (c));

    const int n = (int) param.getNormalisableRange().end + 1;
    const int side = n < 3 ? (index == 0 ? -1 : 1) : index - 1; // -1 LOW, 0 LAST, 1 HIGH
    const float ball = r * 0.42f;
    if (side == 0)
    {
        // Pointing at the viewer: the ball over the bushing, its shadow just below.
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillEllipse (juce::Rectangle<float> (ball * 2.3f, ball * 2.1f).withCentre (c.translated (ball * 0.25f, ball * 0.45f)));
        juce::ColourGradient shine (juce::Colours::white, c.x - ball * 0.45f, c.y - ball * 0.45f, juce::Colour (0xff7a7a76), c.x + ball, c.y + ball, true);
        g.setGradientFill (shine);
        g.fillEllipse (juce::Rectangle<float> (ball * 2.0f, ball * 2.0f).withCentre (c));
        return;
    }

    const float dir = (float) side;
    const auto tip = c.translated (dir * r * 1.7f, 0.0f);
    // The shadow the bat throws on the panel.
    juce::Path shadow;
    shadow.addRoundedRectangle (juce::Rectangle<float> (std::abs (tip.x - c.x) + ball, ball * 1.2f)
                                    .withCentre ({ (c.x + tip.x) * 0.5f + r * 0.12f, c.y + r * 0.35f }), ball * 0.6f);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillPath (shadow);
    // The bat, tapering from the bushing to the ball, lit from above.
    juce::Path bat;
    const float w0 = r * 0.32f, w1 = r * 0.2f;
    bat.startNewSubPath (c.x, c.y - w0);
    bat.lineTo (tip.x, tip.y - w1);
    bat.lineTo (tip.x, tip.y + w1);
    bat.lineTo (c.x, c.y + w0);
    bat.closeSubPath();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff6f6f2), c.x, c.y - w0, juce::Colour (0xff6e6e6a), c.x, c.y + w0, false));
    g.fillPath (bat);
    juce::ColourGradient shine (juce::Colours::white, tip.x - ball * 0.4f, tip.y - ball * 0.5f, juce::Colour (0xff7a7a76), tip.x + ball, tip.y + ball, true);
    g.setGradientFill (shine);
    g.fillEllipse (juce::Rectangle<float> (ball * 2.0f, ball * 2.0f).withCentre (tip));
}

void Toggle::mouseDown (const juce::MouseEvent&)
{
    const int n = (int) param.getNormalisableRange().end + 1;
    attachment.setValueAsCompleteGesture ((float) ((index + 1) % std::max (1, n)));
}


Wheel::Wheel (juce::RangedAudioParameter& p, bool d)
    : param (p), attachment (p, [this] (float v) { value = param.convertTo0to1 (v); repaint(); }), detent (d)
{
    attachment.sendInitialUpdate();
}

void Wheel::paint (juce::Graphics& g)
{
    // A slot cut into the panel: its lit lower lip, the dark well, the shadow under its top edge.
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    const float radius = b.getWidth() * 0.18f;
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.fillRoundedRectangle (b.translated (0.0f, 1.2f), radius);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff020202), 0, b.getY(), juce::Colour (0xff121211), 0, b.getBottom(), false));
    g.fillRoundedRectangle (b, radius);

    // The wheel inside, a cylinder: bright where it faces the viewer, dark where it turns away.
    auto wheel = b.reduced (b.getWidth() * 0.11f, b.getHeight() * 0.014f);
    juce::ColourGradient grad (juce::Colour (0xff3a3a38), 0, wheel.getY(), juce::Colour (0xff3a3a38), 0, wheel.getBottom(), false);
    grad.addColour (0.15, juce::Colour (0xff86867f));
    grad.addColour (0.45, juce::Colour (0xffd8d8d2));
    grad.addColour (0.55, juce::Colour (0xffdcdcd6));
    grad.addColour (0.85, juce::Colour (0xff7a7a74));
    g.setGradientFill (grad);
    g.fillRect (wheel);
    // Its edges in the slot's shadow.
    juce::ColourGradient edges (juce::Colours::black.withAlpha (0.3f), wheel.getX(), 0, juce::Colours::black.withAlpha (0.3f), wheel.getRight(), 0, false);
    edges.addColour (0.15, juce::Colours::black.withAlpha (0.0f));
    edges.addColour (0.85, juce::Colours::black.withAlpha (0.0f));
    g.setGradientFill (edges);
    g.fillRect (wheel);
    // Ribs, and the mark that turns with the wheel.
    // About 75 degrees either way: the mark reaches the slot's ends.
    const float travel = (value - 0.5f) * 2.6f;
    for (int i = -10; i <= 10; ++i)
    {
        const float a = (float) i * 0.14f + travel;
        if (std::abs (a) > 1.45f)
            continue;
        const float y = wheel.getCentreY() - std::sin (a) * wheel.getHeight() * 0.5f;
        g.setColour (juce::Colours::black.withAlpha (0.25f * std::cos (a)));
        g.drawLine (wheel.getX() + 2, y, wheel.getRight() - 2, y, 1.2f);
    }
    if (std::abs (travel) < 1.4f)
    {
        const float y = wheel.getCentreY() - std::sin (travel) * wheel.getHeight() * 0.5f;
        g.setColour (juce::Colour (0xff151515));
        if (detent)
            g.fillRect (wheel.getX() + 2, y - 2.0f, wheel.getWidth() - 4, 4.0f);
        else
            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ wheel.getCentreX(), y }));
    }
}

void Wheel::paintOverChildren (juce::Graphics& g)
{
    // The top edge's shadow falls across the wheel.
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.45f), 0, b.getY(), juce::Colours::black.withAlpha (0.0f), 0, b.getY() + b.getHeight() * 0.07f, false));
    g.fillRoundedRectangle (b, b.getWidth() * 0.18f);
}

void Wheel::mouseDown (const juce::MouseEvent&)
{
    startValue = value;
    attachment.beginGesture();
}

void Wheel::mouseDrag (const juce::MouseEvent& e)
{
    const float v = juce::jlimit (0.0f, 1.0f, startValue - (float) e.getDistanceFromDragStartY() / ((float) getHeight() * 0.9f));
    attachment.setValueAsPartOfGesture (param.convertFrom0to1 (v));
}

void Wheel::mouseUp (const juce::MouseEvent&)
{
    if (detent)
        attachment.setValueAsPartOfGesture (param.convertFrom0to1 (0.5f));
    attachment.endGesture();
}
Panel::Panel (std::shared_ptr<ca72Lib::Synth> s) : synth (std::move (s)), apvts (synth->apvts), look (std::make_unique<Look>())
{
    setLookAndFeel (look.get());
    const float T = TOP;

    // CONTROLLERS.
    addKnob ("tune", 229, T + 273, Dial::Tune);
    addRocker ("oscMod", 444, T + 269, 108, 50, RockerColour::Red);
    addKnob ("glide", 126, T + 480, Dial::Ten);
    addKnob ("modMix", 332, T + 480, Dial::ModMix);

    // OSCILLATOR BANK.
    const float rows[3] { 163, 370, 576 };
    for (int o = 0; o < 3; ++o)
    {
        addSelector ("range" + juce::String (o + 1), 617, T + rows[o], Legends::Ranges);
        addSelector ("wave" + juce::String (o + 1), 1021, T + rows[o], o == 2 ? Legends::Waves3 : Legends::Waves12);
    }
    addKnob ("freq2", 820, T + 373, Dial::Freq, true);
    addKnob ("freq3", 820, T + 581, Dial::Freq, true);
    addRocker ("osc3Control", 480, T + 585, 55, 111, RockerColour::Red, true);

    // MIXER.
    const float mix[5] { 170, 274, 377, 481, 583 };
    addKnob ("vol1", 1220, T + mix[0], Dial::Ten);
    addRocker ("on1", 1419, T + mix[0], 108, 50, RockerColour::Blue);
    addKnob ("vol2", 1220, T + mix[2], Dial::Ten);
    addRocker ("on2", 1419, T + mix[2], 108, 50, RockerColour::Blue);
    addKnob ("vol3", 1220, T + mix[4], Dial::Ten);
    addRocker ("on3", 1419, T + mix[4], 108, 50, RockerColour::Blue);
    addKnob ("noiseVol", 1621, T + mix[3], Dial::Ten);
    addKnob ("extVol", 1621, T + mix[1], Dial::Ten);
    addRocker ("extOn", 1419, T + mix[1], 108, 50, RockerColour::Blue);
    addRocker ("noiseOn", 1419, T + mix[3], 108, 50, RockerColour::Blue);
    addRocker ("noisePink", 1766, T + 481, 44, 114, RockerColour::Blue, true);

    // MODIFIERS.
    addRocker ("filterHi", 1907, T + 169, 108, 48, RockerColour::Red);
    addRocker ("filterMod", 1907, T + 291, 108, 50, RockerColour::Red);
    addRocker ("kbd1", 1907, T + 386, 108, 48, RockerColour::Red);
    addRocker ("kbd2", 1907, T + 486, 108, 48, RockerColour::Red);
    addKnob ("cutoff", 2081, T + 170, Dial::Cutoff);
    addKnob ("emphasis", 2281, T + 170, Dial::Ten);
    addKnob ("contourAmt", 2483, T + 170, Dial::Ten);
    addKnob ("fAttack", 2080, T + 358, Dial::Time);
    addKnob ("fDecay", 2281, T + 358, Dial::Time);
    addKnob ("fSustain", 2483, T + 358, Dial::Ten);
    addKnob ("lAttack", 2080, T + 614, Dial::Time);
    addKnob ("lDecay", 2281, T + 614, Dial::Time);
    addKnob ("lSustain", 2483, T + 614, Dial::Ten);

    // OUTPUT.
    addKnob ("volume", 2769, T + 167, Dial::Ten);
    addRocker ("a440", 2769, T + 375, 112, 50, RockerColour::Blue);
    addKnob ("feedback", 2769, T + 580, Dial::Ten);

    // The strip: the left hand controller, DOUBLE and the output's LEVEL.
    const float S = STRIP_Y;
    auto* pr = new Toggle (*apvts.getParameter ("priority"));
    controls.add (pr);
    priority = pr;
    addAndMakeVisible (pr);
    placed.push_back ({ pr, 264, S + 80, 70, 70 });
    addRocker ("retrigger", 264, S + 165, 82, 38, RockerColour::Ivory);
    addRocker ("glideOn", 264, S + 245, 82, 38, RockerColour::Ivory);
    addRocker ("decay", 264, S + 325, 82, 38, RockerColour::Ivory);
    auto* pw = controls.add (new Wheel (*apvts.getParameter ("pitchWheel"), true));
    auto* mw = controls.add (new Wheel (*apvts.getParameter ("modWheelKnob"), false));
    addAndMakeVisible (pw);
    addAndMakeVisible (mw);
    placed.push_back ({ pw, 424, S + 230, 70, 300 });
    placed.push_back ({ mw, 534, S + 230, 70, 300 });

    // VOICES: the switches stacked as MODIFIERS' are; the knobs' dials with equal margins to them and to the line.
    addRocker ("poly", 772, S + 112, 108, 48, RockerColour::Red);
    addRocker ("unison", 772, S + 232, 108, 48, RockerColour::Red);
    addRocker ("doubled", 772, S + 352, 108, 48, RockerColour::Red);
    addKnob ("detune", 1000, S + 248, Dial::Detune);
    addKnob ("spread", 1263, S + 248, Dial::Percent);
    addKnob ("entropy", 1526, S + 248, Dial::Percent);
    addSelector ("quality", 1938, S + 248, Legends::Quality);
    addKnob ("drive", 2139, S + 248, Dial::Drive);
    addKnob ("level", 2340, S + 248, Dial::Level);

    // Shift-drag snapping: the oscillators' FREQUENCY to whole semitones through the pot's law,
    // TUNE to whole steps, DETUNE to 5 cents.
    for (auto [pid, pot] : { std::pair { "freq2", ca72::freq_dial::Pot (ca72::freq_dial::Pot::Two) },
                             std::pair { "freq3", ca72::freq_dial::Pot (ca72::freq_dial::Pot::Three) } })
        knobs[pid]->snap = [pot] (double v)
        {
            const double end = ca72::freq_dial::DIAL_END;
            const double st = std::round (pot.semitones ((v + end) / (2.0 * end)));
            return juce::jlimit (-end, end, (2.0 * pot.wiper (st) - 1.0) * end);
        };
    knobs["tune"]->snap = [] (double v) { return std::round (v); };
    knobs["detune"]->snap = [] (double v) { return std::round (v / 5.0) * 5.0; };

    // The ON legends, lit while their switch is on.
    const float mixRows[5] { 170, 274, 377, 481, 583 };
    onLabels = {
        { rockers["oscMod"], 496, T + 313, 19 },
        { rockers["on1"], 1471, T + mixRows[0] + 40, 19 },
        { rockers["extOn"], 1471, T + mixRows[1] + 40, 19 },
        { rockers["on2"], 1471, T + mixRows[2] + 40, 19 },
        { rockers["noiseOn"], 1471, T + mixRows[3] + 40, 19 },
        { rockers["on3"], 1471, T + mixRows[4] + 40, 19 },
        { rockers["filterMod"], 1959, T + 329, 19 },
        { rockers["kbd1"], 1959, T + 350, 19 },
        { rockers["kbd2"], 1959, T + 523, 19 },
        { rockers["a440"], 2823, T + 410, 19 },
        { rockers["retrigger"], 303, S + 199, 15 },
        { rockers["glideOn"], 303, S + 279, 15 },
        { rockers["decay"], 303, S + 359, 15 },
        { rockers["poly"], 824, S + 150, 19 },
        { rockers["unison"], 824, S + 270, 19 },
        { rockers["doubled"], 824, S + 390, 19 },
        // The noise switch: WHITE lit when it is off, PINK when on.
        { rockers["noisePink"], 1764, T + 407, 19, "WHITE", false, false },
        { rockers["noisePink"], 1764, T + 558, 19, "PINK", false, true },
        // OSC. 3 CONTROL has no ON: its own legend lights. FILTER MODE: LO or HI, as WHITE/PINK.
        { rockers["osc3Control"], 488, T + 490, 16, "OSC. 3", false, true },
        { rockers["osc3Control"], 488, T + 509, 16, "CONTROL", false, true },
        { rockers["filterHi"], 1860, T + 207, 19, "LO", false, false },
        { rockers["filterHi"], 1954, T + 207, 19, "HI", false, true },
        // NOTE PRIORITY: the position the lever is at.
        { nullptr, 224, S + 110, 15, "LOW", false, true, priority, 0 },
        { nullptr, 264, S + 38, 15, "LAST", false, true, priority, 1 },
        { nullptr, 306, S + 110, 15, "HIGH", false, true, priority, 2 },
    };

    startTimerHz (30);
}

Panel::~Panel()
{
    stopTimer();
    sliderAttachments.clear();
    buttonAttachments.clear();
    controls.clear();
    setLookAndFeel (nullptr);
}

void Panel::addKnob (const juce::String& pid, float x, float y, Dial dial, bool big)
{
    auto* k = new Knob (big, false);
    controls.add (k);
    knobs[pid] = k;
    k->setRotaryParameters (rad (-150.0f), rad (150.0f), true);
    addAndMakeVisible (k);
    sliderAttachments.add (new juce::AudioProcessorValueTreeState::SliderAttachment (apvts, pid, *k));
    if (auto* prm = apvts.getParameter (pid))
        k->setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    const float d = big ? 130.0f : 108.0f;
    placed.push_back ({ k, x, y, d, d });
    dials.emplace_back (x, y, dial, big);
}

void Panel::addSelector (const juce::String& pid, float x, float y, Legends legends)
{
    auto* k = new Knob (false, true);
    controls.add (k);
    const float span = legends == Legends::Quality ? 45.0f : 83.75f;
    k->setRotaryParameters (rad (-span), rad (span), true);
    addAndMakeVisible (k);
    sliderAttachments.add (new juce::AudioProcessorValueTreeState::SliderAttachment (apvts, pid, *k));
    const float d = legends == Legends::Quality ? 84.0f : 96.0f;
    placed.push_back ({ k, x, y, d, d });
    selectors.emplace_back (x, y, legends);
}

void Panel::addRocker (const juce::String& pid, float x, float y, float w, float h, RockerColour c, bool vertical)
{
    const float pad = 0.0f;
    auto* r = new Rocker (c, vertical, pad / (w + 2.0f * pad));
    controls.add (r);
    addAndMakeVisible (r);
    r->onStateChange = [this] { repaint(); }; // its shadow is drawn by the panel
    buttonAttachments.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (apvts, pid, *r));
    placed.push_back ({ r, x, y, w + 2.0f * pad, h + 2.0f * pad, pad });
    rockers[pid] = r;
}

void Panel::resized()
{
    const float s = (float) getWidth() / W;
    for (const auto& p : placed)
        p.comp->setBounds (juce::Rectangle<float> (p.w * s, p.h * s).withCentre ({ p.x * s, p.y * s }).toNearestInt());

    // While the window is dragged the old background is stretched; it is redrawn once the size settles.
    if (background.isValid())
    {
        rebuildTicks = 5;
        repaint();
        return;
    }
    rebuildBackground();
}

void Panel::rebuildBackground()
{
    const float s = (float) getWidth() / W;
    // the scale the panel is painted at, which on Windows and Linux includes the host's plugin scale
    // before the first paint, the display's scale too, so a Retina panel is not built at 1x and then again
    float firstScale = juce::Component::getApproximateScaleFactorForComponent (this);
    if (const auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        firstScale *= (float) d->scale;
    const float scale = backgroundScale = paintScale > 0.0f ? paintScale : firstScale;
    background = juce::Image (juce::Image::ARGB, std::max (1, juce::roundToInt (getWidth() * scale)), std::max (1, juce::roundToInt (getHeight() * scale)), true);
    juce::Graphics g (background);
    // One texel is half a point on any display, so the grain looks the same at 1x and on Retina.
    texelUnits = 0.5f / s;
    g.addTransform (juce::AffineTransform::scale (s * scale));
    drawPanel (g);
    repaint();
}

void Panel::paint (juce::Graphics& g)
{
    paintScale = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (background.isValid() && rebuildTicks == 0 && std::abs (paintScale - backgroundScale) > 0.01f)
        rebuildBackground();
    if (background.isValid())
    {
        // one image pixel per device pixel, so the cached lettering is not resampled
        juce::Graphics::ScopedSaveState keep (g);
        g.drawImageTransformed (background, juce::AffineTransform::scale ((float) getWidth() / (float) background.getWidth(),
                                                                          (float) getHeight() / (float) background.getHeight()));
    }
    g.addTransform (juce::AffineTransform::scale ((float) getWidth() / W));

    for (const auto& l : onLabels)
    {
        const bool lit = l.toggle != nullptr ? l.toggle->getIndex() == l.position
                                             : l.rocker->getToggleState() == l.litWhenOn;
        // Most repaints are a knob or a switch; the labels elsewhere are skipped.
        const auto area = l.alignRight ? juce::Rectangle<float> (l.x - 200.0f, l.y - l.size, 204.0f, l.size * 2.0f)
                                       : juce::Rectangle<float> (l.x - 300.0f, l.y - l.size, 600.0f, l.size * 2.0f);
        if (! g.clipRegionIntersects (area.toNearestInt()))
            continue;
        auto draw = [&] (float x, float y, bool shadow)
        {
            if (l.alignRight)
                on (g, x, y, l.size, shadow);
            else
                legend (g, x, y, l.text, l.size, shadow);
        };
        if (lit)
        {
            const auto colour = l.rocker != nullptr ? l.rocker->colour : RockerColour::Ivory;
            const juce::Colour neon = colour == RockerColour::Blue ? juce::Colour (0xff3ab0ff)
                                      : colour == RockerColour::Red ? juce::Colour (0xffff5a2a)
                                                                    : juce::Colour (0xffffc46a);
            // A soft halo of offset copies, then the letters in the neon colour.
            for (float d : { 3.0f, 2.0f, 1.0f })
            {
                g.setColour (neon.withAlpha (0.16f));
                for (auto off : { juce::Point<float> (-d, 0), { d, 0 }, { 0, -d }, { 0, d } })
                    draw (l.x + off.x, l.y + off.y, false);
            }
            g.setColour (neon.brighter (0.3f));
        }
        else
        {
            g.setColour (INK);
        }
        draw (l.x, l.y, ! lit);
    }

    // Rocker shadows, the light straight above: the raised half tilts up from the pivot, so below it
    // falls a wedge, nothing at the pivot and deepest under the raised end; the pressed half sits
    // almost flush.
    for (const auto& p : placed)
    {
        auto* rk = dynamic_cast<Rocker*> (p.comp);
        if (rk == nullptr)
            continue;
        const auto body = juce::Rectangle<float> (p.w - 2.0f * p.pad, p.h - 2.0f * p.pad).withCentre ({ p.x, p.y });
        const bool on = rk->getToggleState();
        const float depth = std::max (p.w, p.h) * 0.3f;
        if (! g.clipRegionIntersects (body.withHeight (body.getHeight() + depth + 3.0f).toNearestInt()))
            continue;

        // Flush contact shadow along the whole bottom edge.
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRect (body.getX() + 1.0f, body.getBottom(), body.getWidth() - 2.0f, 2.5f);

        for (int k = 1; k <= 5; ++k)
        {
            const float d = depth * (float) k / 5.0f;
            juce::Path wedge;
            if (rk->vertical)
            {
                // Only a raised bottom end shows below the switch; a raised top shades its own face.
                if (on)
                    continue;
                wedge.addRectangle (body.getX(), body.getBottom(), body.getWidth(), d);
            }
            else
            {
                const float pivot = body.getCentreX();
                const float end = on ? body.getX() : body.getRight();
                wedge.startNewSubPath (pivot, body.getBottom());
                wedge.lineTo (end, body.getBottom());
                wedge.lineTo (end, body.getBottom() + d);
                wedge.closeSubPath();
            }
            g.setColour (juce::Colours::black.withAlpha (0.3f));
            g.fillPath (wedge);
        }
    }
}

void Panel::drawPanel (juce::Graphics& g) const
{
    // Wood: the rail between the panel and the strip.
    auto wood = [&g] (juce::Rectangle<float> r, int seed)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6b3a1e), 0, r.getY(), juce::Colour (0xff3d1e0e), 0, r.getBottom(), false));
        g.fillRect (r);
        juce::Random rnd (seed);
        for (int i = 0; i < 90; ++i)
        {
            const float y = r.getY() + rnd.nextFloat() * r.getHeight();
            juce::Path grain;
            grain.startNewSubPath (r.getX(), y);
            for (float x = r.getX(); x <= r.getRight() + 120.0f; x += 120.0f)
                grain.lineTo (x, y + std::sin (x * 0.002f + (float) i) * 4.0f + rnd.nextFloat() * 1.5f);
            g.setColour ((rnd.nextBool() ? juce::Colour (0xff2a1408) : juce::Colour (0xff8a5230)).withAlpha (0.18f + rnd.nextFloat() * 0.2f));
            g.strokePath (grain, juce::PathStrokeType (1.0f + rnd.nextFloat() * 2.5f));
        }
    };
    wood ({ 0, 0, W, TOP }, 1);
    wood ({ 0, PANEL_H, W, BAR_H }, 2);

    // The panels.
    for (auto r : { juce::Rectangle<float> (0, TOP, W, PANEL_H - TOP), juce::Rectangle<float> (0, STRIP_Y, W, STRIP_H) })
    {
        g.setGradientFill (juce::ColourGradient (PANEL.brighter (0.05f), 0, r.getY(), PANEL.darker (0.25f), 0, r.getBottom(), false));
        g.fillRect (r);
        // The bumped noise, a texel to half a point.
        g.setFillType (juce::FillType (panelTexture(), juce::AffineTransform::scale (texelUnits)));
        g.fillRect (r);

        // Light as it falls on a real panel. A broad key light from the upper left...
        juce::ColourGradient key (juce::Colours::white.withAlpha (0.045f), r.getX() + r.getWidth() * 0.22f, r.getY() - r.getHeight() * 0.25f,
                                  juce::Colours::white.withAlpha (0.0f), r.getX() + r.getWidth() * 0.22f + r.getWidth() * 0.55f, r.getY(), true);
        g.setGradientFill (key);
        g.fillRect (r);
        // ...a faint diagonal sheen band...
        juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.0f), r.getX(), r.getY(), juce::Colours::white.withAlpha (0.0f), r.getRight(), r.getBottom(), false);
        sheen.addColour (0.30, juce::Colours::white.withAlpha (0.0f));
        sheen.addColour (0.38, juce::Colours::white.withAlpha (0.018f));
        sheen.addColour (0.46, juce::Colours::white.withAlpha (0.0f));
        g.setGradientFill (sheen);
        g.fillRect (r);
        // ...falling off towards the bottom...
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.0f), 0, r.getY() + r.getHeight() * 0.35f, juce::Colours::black.withAlpha (0.25f), 0, r.getBottom(), false));
        g.fillRect (r);
        // ...and a vignette at the sides.
        juce::ColourGradient sides (juce::Colours::black.withAlpha (0.3f), r.getX(), 0, juce::Colours::black.withAlpha (0.3f), r.getRight(), 0, false);
        sides.addColour (0.08, juce::Colours::black.withAlpha (0.0f));
        sides.addColour (0.92, juce::Colours::black.withAlpha (0.0f));
        g.setGradientFill (sides);
        g.fillRect (r);
        // Edges: a lit top lip, and a shadow where the panel meets the wood.
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.10f), 0, r.getY(), juce::Colours::white.withAlpha (0.0f), 0, r.getY() + 6.0f, false));
        g.fillRect (r.withHeight (6.0f));
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.0f), 0, r.getBottom() - 14.0f, juce::Colours::black.withAlpha (0.7f), 0, r.getBottom(), false));
        g.fillRect (r.withTop (r.getBottom() - 14.0f));
    }

    // The controls' shadows, cached with the background.
    for (const auto& p : placed)
    {
        juce::Path shape;
        const auto r = juce::Rectangle<float> (p.w - 2.0f * p.pad, p.h - 2.0f * p.pad).withCentre ({ p.x, p.y });
        if (dynamic_cast<Wheel*> (p.comp) != nullptr || dynamic_cast<Rocker*> (p.comp) != nullptr)
            continue; // a slot casts no shadow; rockers follow their state (paint)
        if (dynamic_cast<Knob*> (p.comp) != nullptr)
            shape.addEllipse (r.reduced (p.w * 0.04f));
        else if (dynamic_cast<Toggle*> (p.comp) != nullptr)
            shape.addEllipse (juce::Rectangle<float> (p.w * 0.46f, p.w * 0.46f).withCentre ({ p.x, p.y }));
        else
            shape.addRoundedRectangle (r.reduced (2.0f), 4.0f);
        const float sw = p.w - 2.0f * p.pad;
        // Light from straight above: a long soft shadow below, and a tight one where the control meets the panel.
        juce::DropShadow (juce::Colours::black.withAlpha (0.8f), (int) (sw * 0.22f), { 0, (int) (sw * 0.18f) + 3 })
            .drawForPath (g, shape);
        juce::DropShadow (juce::Colours::black.withAlpha (0.6f), (int) (sw * 0.06f), { 0, (int) (sw * 0.04f) + 1 })
            .drawForPath (g, shape);
    }

    // Name plate: black anodised aluminium, brushed, bevelled, screwed to the wood; the
    // lettering inlaid in cream with a shadow below and a catch of light above.
    {
        // In the strip's last column, under the panel's OUTPUT column.
        const auto plate = juce::Rectangle<float> (261, 82).withCentre ({ (2578.0f + W) / 2.0f, STRIP_Y + 248.0f });
        juce::Path shape;
        shape.addRoundedRectangle (plate, 5.0f);
        juce::DropShadow (juce::Colours::black.withAlpha (0.8f), 14, { 3, 7 }).drawForPath (g, shape);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c2c2b), 0, plate.getY(), juce::Colour (0xff0b0b0b), 0, plate.getBottom(), false));
        g.fillPath (shape);
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (shape);
            juce::Random rnd (7);
            for (float y = plate.getY(); y < plate.getBottom(); y += 0.7f)
            {
                g.setColour (juce::Colours::white.withAlpha (0.015f + rnd.nextFloat() * 0.035f));
                g.drawHorizontalLine ((int) y, plate.getX(), plate.getRight());
            }
            // A soft sheen across the upper half, as the light falls on it.
            juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.0f), plate.getX(), plate.getY(), juce::Colours::white.withAlpha (0.0f), plate.getRight(), plate.getY(), false);
            sheen.addColour (0.35, juce::Colours::white.withAlpha (0.10f));
            sheen.addColour (0.55, juce::Colours::white.withAlpha (0.02f));
            g.setGradientFill (sheen);
            g.fillRect (plate.withHeight (plate.getHeight() * 0.55f));
        }
        // Bevel: lit top edge, dark lower edge, a fine black outline.
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.drawLine (plate.getX() + 5, plate.getY() + 1.2f, plate.getRight() - 5, plate.getY() + 1.2f, 1.5f);
        g.setColour (juce::Colours::black.withAlpha (0.9f));
        g.drawLine (plate.getX() + 5, plate.getBottom() - 1.2f, plate.getRight() - 5, plate.getBottom() - 1.2f, 2.0f);
        g.setColour (juce::Colour (0xff000000));
        g.strokePath (shape, juce::PathStrokeType (1.5f));
        // Two screws, slots at odd angles.
        for (auto [sx, ang] : { std::pair { plate.getX() + 15.0f, 0.6f }, std::pair { plate.getRight() - 15.0f, -0.3f } })
        {
            const juce::Point<float> sc (sx, plate.getCentreY());
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd8d8d2), sc.x - 4, sc.y - 4, juce::Colour (0xff4a4a46), sc.x + 4, sc.y + 4, false));
            g.fillEllipse (juce::Rectangle<float> (10, 10).withCentre (sc));
            g.setColour (juce::Colour (0xff1a1a1a));
            g.drawLine ({ sc.getPointOnCircumference (4.0f, ang), sc.getPointOnCircumference (4.0f, ang + juce::MathConstants<float>::pi) }, 1.6f);
        }
        auto letters = [&] (float y, const juce::String& t, float size)
        {
            g.setColour (juce::Colours::black.withAlpha (0.85f));
            legend (g, plate.getCentreX(), y + 1.6f, t, size, false);
            g.setColour (juce::Colours::white.withAlpha (0.18f));
            legend (g, plate.getCentreX(), y - 0.8f, t, size, false);
            g.setColour (juce::Colour (0xffe8e1cf));
            legend (g, plate.getCentreX(), y, t, size, false);
        };
        letters (plate.getY() + 33, "CA-72", 40.0f);
        letters (plate.getY() + 66, "IDLE FOUNDRY", 13.0f);
    }

    const float T = TOP;
    g.setColour (LINE);
    // Section dividers, broken where a label or a switch sits across one.
    auto divider = [&] (float x, std::initializer_list<std::pair<float, float>> gaps)
    {
        float y = 9.0f;
        for (auto [a, b] : gaps)
        {
            paintedLine (g, { x, T + y }, { x, T + a }, 4.0f, texelUnits);
            y = b;
        }
        paintedLine (g, { x, T + y }, { x, T + PH - 7.0f }, 4.0f, texelUnits);
    };
    divider (445, { { 198, 297 } });
    divider (1122, {});
    divider (1908, { { 98, 319 }, { 356, 414 }, { 420, 514 } });
    divider (2578, {});
    paintedLine (g, { 1976, T + 466 }, { 2578, T + 466 }, 4.0f, texelUnits);

    g.setColour (INK);
    const float titleY = T + PH - 46.0f - 52.0f * 0.36f;
    legend (g, (45 + 406) / 2.0f, titleY, "CONTROLLERS", 52.0f);
    legend (g, (532 + 1028) / 2.0f, titleY, "OSCILLATOR BANK", 52.0f);
    legend (g, (1442 + 1591) / 2.0f, titleY, "MIXER", 52.0f);
    legend (g, (2111 + 2382) / 2.0f, titleY, "MODIFIERS", 52.0f);
    legend (g, (2670 + 2868) / 2.0f, titleY, "OUTPUT", 52.0f);

    // Legends (upstream art.rs).
    legend (g, 229, T + 162, "TUNE");
    legend (g, 446, T + 212, "OSCILLATOR");
    legend (g, 446, T + 232, "MODULATION");
    legend (g, 128, T + 381, "GLIDE");
    legend (g, 332, T + 381, "MODULATION MIX");
    legend (g, 618, T + 57, "RANGE");
    legend (g, 821, T + 57, "OSCILLATOR -1");
    legend (g, 821, T + 81, "FREQUENCY");
    legend (g, 1022, T + 57, "WAVEFORM");
    legend (g, 822, T + 260, "OSCILLATOR -2");
    legend (g, 822, T + 471, "OSCILLATOR -3");
    legend (g, 1221, T + 59, "VOLUME");
    const float mix[5] { 170, 274, 377, 481, 583 };
    for (int i = 0; i < 5; ++i)
    {
        paintedLine (g, { i % 2 == 0 ? 1316.0f : 1477.0f, T + mix[i] }, { i % 2 == 0 ? 1361.0f : 1522.0f, T + mix[i] }, 4.0f, texelUnits);
    }
    legend (g, 1622, T + 385, "NOISE VOLUME");
    legend (g, 1620, T + 144, "EXTERNAL");
    legend (g, 1622, T + 164, "INPUT VOLUME");
    legend (g, 1910, T + 114, "FILTER");
    legend (g, 1910, T + 134, "MODE");
    legend (g, 1910, T + 236, "FILTER");
    legend (g, 1910, T + 256, "MODULATION");
    legend (g, 1837, T + 385, "1");
    legend (g, 1910, T + 431, "KEYBOARD");
    legend (g, 1910, T + 448, "CONTROL");
    legend (g, 1837, T + 485, "2");
    legend (g, 2282, T + 31, "FILTER", 31.0f);
    legend (g, 2081, T + 55, "CUTOFF FREQUENCY");
    legend (g, 2280, T + 55, "EMPHASIS");
    legend (g, 2482, T + 32, "AMOUNT");
    legend (g, 2482, T + 54, "OF CONTOUR");
    for (float ly : { 266.0f, 524.0f })
    {
        legend (g, 2081, T + ly, "ATTACK TIME");
        legend (g, 2281, T + ly, "DECAY TIME");
        legend (g, 2483, T + ly, "SUSTAIN LEVEL");
    }
    legend (g, 2284, T + 500, "LOUDNESS CONTOUR", 27.0f);
    legend (g, 2769, T + 51, "VOLUME");
    legend (g, 2769, T + 325, "A-440");
    legend (g, 2769, T + 485, "FEEDBACK");

    // Strip.
    const float S = STRIP_Y;
    g.setColour (LINE);
    paintedLine (g, { 640, S + 20 }, { 640, S + STRIP_H - 20 }, 4.0f, texelUnits);
    paintedLine (g, { 1700, S + 20 }, { 1700, S + STRIP_H - 20 }, 4.0f, texelUnits);
    paintedLine (g, { 2578, S + 20 }, { 2578, S + STRIP_H - 20 }, 4.0f, texelUnits);
    g.setColour (INK);
    legend (g, 124, S + 80, "PRIORITY");
    legend (g, 124, S + 165, "RETRIGGER");
    legend (g, 124, S + 245, "GLIDE");
    legend (g, 124, S + 325, "DECAY");
    legend (g, 424, S + 410, "PITCH");
    legend (g, 534, S + 410, "MOD.");
    legend (g, 775, S + 72, "POLY");
    legend (g, 775, S + 192, "UNISON");
    legend (g, 775, S + 312, "DOUBLE");
    legend (g, 1000, S + 133, "DETUNE");
    legend (g, 1263, S + 133, "WIDTH");
    legend (g, 1526, S + 133, "ENTROPY");
    legend (g, 1938, S + 133, "QUALITY");
    legend (g, 2139, S + 133, "DRIVE");
    legend (g, 2340, S + 133, "LEVEL");
    legend (g, (640 + 1700) / 2.0f, S + STRIP_H - 46.0f - 52.0f * 0.36f, "VOICES", 52.0f);
    legend (g, (1700 + 2578) / 2.0f, S + STRIP_H - 46.0f - 52.0f * 0.36f, "OUTPUT", 52.0f);

    // Dial marks.
    for (const auto& [cx, cy, dial, big] : dials)
    {
        const float r0 = big ? 72.0f : 58.0f, r1 = big ? 85.0f : 71.0f, rl = big ? 98.0f : 86.0f;
        auto num = [&] (float deg, const juce::String& t, float r = 0.0f)
        {
            const float a = rad (deg);
            const float rr = r > 0.0f ? r : rl;
            legend (g, cx + rr * std::sin (a), cy - rr * std::cos (a) + 1.0f, t, 15.0f);
        };
        if (dial == Dial::Freq)
        {
            for (int k = -7; k <= 7; ++k)
                tick (g, cx, cy, (float) k * 20.0f, k == 0 ? 71.0f : 72.0f, k == 0 ? 93.0f : 85.0f, k == 0 ? 7.0f : 4.0f);
            tick (g, cx, cy, -156.0f, 72.0f, 85.0f, 4.0f);
            tick (g, cx, cy, 156.0f, 72.0f, 85.0f, 4.0f);
            for (int k : { -7, -5, -3, -1, 1, 3, 5, 7 })
                num ((float) k * 20.0f, k < 0 ? "-" + juce::String (-k) : juce::String (k));
            continue;
        }
        if (dial == Dial::Time)
        {
            for (float a : { -150.0f, -120.0f, -105.0f, -90.0f, -60.0f, -30.0f, 0.0f, 30.0f, 49.0f, 67.0f, 86.0f, 108.0f, 148.0f })
                tick (g, cx, cy, a, 58.0f, a == -90.0f ? 78.0f : 71.0f, 4.0f);
        }
        else
        {
            const int n = dial == Dial::Detune || dial == Dial::Percent || dial == Dial::Drive ? 9 : 11;
            for (int k = 0; k < n; ++k)
                tick (g, cx, cy, -150.0f + 300.0f * (float) k / (float) (n - 1), r0, r1, 4.0f);
        }
        switch (dial)
        {
            case Dial::Ten:
            case Dial::ModMix:
                for (int i = 0; i <= 5; ++i)
                    num (-150.0f + 60.0f * (float) i, juce::String (i * 2));
                if (dial == Dial::ModMix)
                {
                    legend (g, cx - 50, cy + 100, "OSC. 3", 15.0f);
                    legend (g, cx + 49, cy + 100, "NOISE", 15.0f);
                }
                break;
            case Dial::Tune:
                for (int i = -2; i <= 2; ++i)
                    num ((float) i * 60.0f, i < 0 ? "-" + juce::String (-i) : juce::String (i));
                break;
            case Dial::Cutoff:
                for (int i = -4; i <= 4; i += 2)
                    num ((float) i * 30.0f, i < 0 ? "-" + juce::String (-i) : juce::String (i));
                break;
            case Dial::Time:
                num (-120.0f, "10");
                num (-90.0f, "200");
                num (-30.0f, "600", 80.0f);
                num (30.0f, "1", 80.0f);
                num (67.0f, "5");
                num (108.0f, "10");
                legend (g, cx - 61, cy + 75, "M-SEC.", 15.0f);
                legend (g, cx + 50, cy + 75, "SEC.", 15.0f);
                break;
            case Dial::Detune:
                for (int i = 0; i <= 4; ++i)
                    num (-150.0f + 75.0f * (float) i, juce::String (i * 25));
                break;
            case Dial::Percent:
                for (int i = 0; i <= 4; ++i)
                    num (-150.0f + 75.0f * (float) i, juce::String (i * 25));
                break;
            case Dial::Drive:
                for (int i = 0; i <= 4; ++i)
                    num (-150.0f + 75.0f * (float) i, juce::String (i * 6));
                break;
            case Dial::Level:
                for (int db : { -30, -20, -10, 0, 10 })
                    num (-150.0f + ((float) db + 30.0f) * 300.0f / 42.0f, juce::String (db));
                break;
            case Dial::Freq:
                break;
        }
    }

    // Selector ticks and legends.
    for (const auto& [cx, cy, set] : selectors)
    {
        if (set == Legends::Quality)
        {
            const char* names[3] { "LO", "HI", "ULTRA" };
            for (int i = 0; i < 3; ++i)
            {
                const float deg = -45.0f + 45.0f * (float) i;
                tick (g, cx, cy, deg, 50.0f, 60.0f, 4.0f);
                const float a = rad (deg);
                legend (g, cx + 72.0f * std::sin (a), cy - 72.0f * std::cos (a) - 6.0f, names[i], 13.0f);
            }
            continue;
        }
        const float six[6] { -83.75f, -50.25f, -16.75f, 16.75f, 50.25f, 83.75f };
        const char* ranges[6] { "LO", "32'", "16'", "8'", "4'", "2'" };
        const int waves12[6] { 0, 1, 2, 3, 4, 5 };
        const int waves3[6] { 0, 6, 2, 3, 4, 5 };
        for (int i = 0; i < 6; ++i)
        {
            tick (g, cx, cy, six[i], 61.0f, 70.0f, 4.0f);
            const float a = rad (six[i]);
            const float lx = cx + 89.0f * std::sin (a), ly = cy - 89.0f * std::cos (a);
            if (set == Legends::Ranges)
                legend (g, lx, ly, ranges[i], 17.0f);
            else
                wavePath (g, lx, ly, set == Legends::Waves3 ? waves3[i] : waves12[i]);
        }
    }
}

void Panel::timerCallback()
{
    uint32_t lit = 0;
    for (size_t i = 0; i < onLabels.size(); ++i)
        if (onLabels[i].toggle != nullptr ? onLabels[i].toggle->getIndex() == onLabels[i].position
                                          : onLabels[i].rocker->getToggleState() == onLabels[i].litWhenOn)
            lit |= 1u << i;
    if (lit != litMask)
    {
        litMask = lit;
        repaint();
    }
    if (rebuildTicks > 0 && --rebuildTicks == 0)
        rebuildBackground();
    if (onEdited)
        onEdited();
}

} // namespace ca72ui

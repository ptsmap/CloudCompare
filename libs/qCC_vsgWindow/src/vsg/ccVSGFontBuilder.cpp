// ##########################################################################
// #                                                                        #
// #                            CLOUDCOMPARE                                #
// #                                                                        #
// #  This program is free software; you can redistribute it and/or modify  #
// #  it under the terms of the GNU General Public License as published by  #
// #  the Free Software Foundation; version 2 or later of the License.      #
// #                                                                        #
// #  This program is distributed in the hope that it will be useful,       #
// #  but WITHOUT ANY WARRANTY; without even the implied warranty of        #
// #  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the          #
// #  GNU General Public License for more details.                          #
// #                                                                        #
// #          COPYRIGHT: CloudCompare project                               #
// #                                                                        #
// ##########################################################################

// Local
#include <vsg/ccVSGFontBuilder.h>

// VSG
#include <vsg/all.h>

// Qt
#include <QFileInfo>

#ifdef CC_VSG_HAS_FREETYPE

// freetype
#include <ft2build.h>
#include FT_FREETYPE_H

// FreeType >= 2.11 provides an SDF renderer, but (up to 2.14 at least) it does
// not define the matching FT_LOAD_TARGET_ convenience macro
#ifndef FT_LOAD_TARGET_SDF
#define FT_LOAD_TARGET_SDF FT_LOAD_TARGET_( FT_RENDER_MODE_SDF )
#endif

// system
#include <algorithm>
#include <cmath>

namespace
{
	//! 26.6 fixed point -> float
	inline float fromF266(FT_Pos v)
	{
		return static_cast<float>(v) / 64.0f;
	}
} // namespace

QString ccVSGFontBuilder::defaultFontFile()
{
	// Hiragino Sans GB covers ASCII *and* CJK, so the character range can be
	// extended later without changing the file.
	static const char* candidates[] = {
	    "/System/Library/Fonts/Hiragino Sans GB.ttc",
	    "/System/Library/Fonts/Helvetica.ttc",
	    "/Library/Fonts/Arial.ttf",
	    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"};

	for (const char* path : candidates)
	{
		if (QFileInfo::exists(QString::fromUtf8(path)))
		{
			return QString::fromUtf8(path);
		}
	}

	return {};
}

vsg::ref_ptr<vsg::Font> ccVSGFontBuilder::buildFont(const QString& fontFile,
                                                    uint32_t       firstChar,
                                                    uint32_t       lastChar,
                                                    uint32_t       pixelHeight)
{
	if (fontFile.isEmpty() || !QFileInfo::exists(fontFile) || lastChar < firstChar)
	{
		return {};
	}

	FT_Library library = nullptr;
	if (FT_Init_FreeType(&library) != 0)
	{
		return {};
	}

	FT_Face face = nullptr;
	if (FT_New_Face(library, fontFile.toUtf8().constData(), 0, &face) != 0)
	{
		FT_Done_FreeType(library);
		return {};
	}

	if (FT_Set_Pixel_Sizes(face, 0, pixelHeight) != 0)
	{
		FT_Done_Face(face);
		FT_Done_FreeType(library);
		return {};
	}

	const uint32_t charCount = lastChar - firstChar + 1;

	// ------------------------------------------------------------------
	// 1st pass: rasterize every glyph to find the atlas cell size
	// ------------------------------------------------------------------
	uint32_t maxGlyphW = 1;
	uint32_t maxGlyphH = 1;

	for (uint32_t i = 0; i < charCount; ++i)
	{
		if (FT_Load_Char(face, firstChar + i, FT_LOAD_RENDER | FT_LOAD_TARGET_SDF) != 0)
		{
			continue;
		}
		maxGlyphW = std::max(maxGlyphW, static_cast<uint32_t>(face->glyph->bitmap.width));
		maxGlyphH = std::max(maxGlyphH, static_cast<uint32_t>(face->glyph->bitmap.rows));
	}

	// one pixel of padding around each cell avoids bleeding between glyphs
	constexpr uint32_t padding = 2;
	const uint32_t     cellW   = maxGlyphW + padding;
	const uint32_t     cellH   = maxGlyphH + padding;

	constexpr uint32_t columns = 16;
	const uint32_t     rows    = (charCount + columns - 1) / columns;

	const uint32_t atlasW = columns * cellW;
	const uint32_t atlasH = rows * cellH;

	// ------------------------------------------------------------------
	// the atlas holds a **signed distance field**: 0 on the glyph edge,
	// positive inside, negative outside. VSG's text shader reads it as such
	// (see vsg/text/shaders: it thresholds 'distance_from_edge' at 0), so the
	// data must be signed (SNORM) - a plain coverage map would render as
	// translucent blocks.
	// ------------------------------------------------------------------
	auto atlas = vsg::vec4Array2D::create(atlasW, atlasH);
	{
		auto properties   = atlas->properties;
		properties.format = VK_FORMAT_R32G32B32A32_SFLOAT;
		atlas->properties = properties;
	}

	auto glyphMetrics = vsg::GlyphMetricsArray::create(charCount);
	auto charmap      = vsg::uintArray::create(lastChar + 1);

	// all metrics are normalized by the line height, so that vsg::Font::height
	// ends up being 1.0 - the layout then scales the text with its 'horizontal'
	// / 'vertical' vectors, which we express directly in overlay pixels.
	const float lineHeightPx = fromF266(face->size->metrics.height) > 0.0f
	                             ? fromF266(face->size->metrics.height)
	                             : static_cast<float>(pixelHeight);
	const float invLineHeight = 1.0f / lineHeightPx;

	// ------------------------------------------------------------------
	// 2nd pass: rasterize and copy the glyphs into the atlas
	// ------------------------------------------------------------------
	for (uint32_t i = 0; i < charCount; ++i)
	{
		const uint32_t charcode = firstChar + i;

		if (FT_Load_Char(face, charcode, FT_LOAD_RENDER | FT_LOAD_TARGET_SDF) != 0)
		{
			continue;
		}

		FT_GlyphSlot slot   = face->glyph;
		FT_Bitmap&   bitmap = slot->bitmap;

		const uint32_t col = i % columns;
		const uint32_t row = i / columns;
		const uint32_t x0  = col * cellW + 1;
		const uint32_t y0  = row * cellH + 1;

		if (bitmap.buffer && bitmap.width > 0 && bitmap.rows > 0)
		{
			for (unsigned int gy = 0; gy < bitmap.rows; ++gy)
			{
				const unsigned char* src = bitmap.buffer + static_cast<std::size_t>(gy) * static_cast<std::size_t>(bitmap.pitch);

				for (unsigned int gx = 0; gx < bitmap.width; ++gx)
				{
					// FreeType's SDF output: 128 = on the edge, > 128 inside,
					// < 128 outside, 16 units per pixel. Converted to a float
					// distance in pixels, so that 0 is exactly on the edge -
					// which is what VSG's text shader thresholds at.
					const float d = (static_cast<float>(src[gx]) - 128.0f) / 16.0f;

					atlas->at(static_cast<uint32_t>(x0 + gx), static_cast<uint32_t>(y0 + gy)) =
					    vsg::vec4(d, d, d, d);
				}
			}
		}

		(*charmap)[charcode] = i;

		// With FT_Set_Pixel_Sizes() all the glyph metrics are in 26.6 fixed
		// point. The outline metrics (and not the padded SDF bitmap size)
		// describe the visible glyph, which is what the layout uses to size the
		// quads.
		const float glyphWpx = fromF266(slot->metrics.width);
		const float glyphHpx = fromF266(slot->metrics.height);

		// the SDF bitmap is the glyph bounding box padded by the spread on both
		// sides: the quad must therefore sample only the inner region
		const float insetX = std::max(0.0f, (static_cast<float>(bitmap.width) - glyphWpx) * 0.5f);
		const float insetY = std::max(0.0f, (static_cast<float>(bitmap.rows) - glyphHpx) * 0.5f);

		vsg::GlyphMetrics& gm = (*glyphMetrics)[i];

		gm.width        = glyphWpx * invLineHeight;
		gm.height       = glyphHpx * invLineHeight;
		gm.horiAdvance  = fromF266(slot->metrics.horiAdvance) * invLineHeight;
		gm.horiBearingX = fromF266(slot->metrics.horiBearingX) * invLineHeight;
		gm.horiBearingY = fromF266(slot->metrics.horiBearingY) * invLineHeight;
		gm.vertAdvance  = fromF266(slot->metrics.vertAdvance) * invLineHeight;
		gm.vertBearingX = fromF266(slot->metrics.vertBearingX) * invLineHeight;
		gm.vertBearingY = fromF266(slot->metrics.vertBearingY) * invLineHeight;

		// uvrect = (min x, min y, max x, max y) of the **glyph bounding box** in
		// normalized atlas coordinates (the spread margin is excluded)
		const float u0 = static_cast<float>(static_cast<float>(x0) + insetX) / static_cast<float>(atlasW);
		const float v0 = static_cast<float>(static_cast<float>(y0) + insetY) / static_cast<float>(atlasH);
		const float u1 = static_cast<float>(static_cast<float>(x0) + insetX + glyphWpx) / static_cast<float>(atlasW);
		const float v1 = static_cast<float>(static_cast<float>(y0) + insetY + glyphHpx) / static_cast<float>(atlasH);
		gm.uvrect.set(u0, v0, u1, v1);
	}

	auto font = vsg::Font::create();
	font->ascender  = fromF266(face->size->metrics.ascender) * invLineHeight;
	font->descender = -fromF266(face->size->metrics.descender) * invLineHeight;
	font->height    = 1.0f;
	font->atlas        = atlas;
	font->glyphMetrics = glyphMetrics;
	font->charmap      = charmap;
	font->createFontImages();

	FT_Done_Face(face);
	FT_Done_FreeType(library);

	return font;
}

#else // !CC_VSG_HAS_FREETYPE

QString ccVSGFontBuilder::defaultFontFile()
{
	return {};
}

vsg::ref_ptr<vsg::Font> ccVSGFontBuilder::buildFont(const QString&, uint32_t, uint32_t, uint32_t)
{
	return {};
}

#endif

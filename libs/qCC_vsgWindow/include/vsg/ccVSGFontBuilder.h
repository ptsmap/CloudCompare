#pragma once
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
#include <qCC_vsgWindow.h>

// VSG
#include <vsg/core/ref_ptr.h>
#include <vsg/text/Font.h>

// Qt
#include <QString>

//! Builds a vsg::Font from a system font file (freetype)
/** VSG ships with a text shader set that expects a **glyph atlas** plus the
    matching metrics (see vsg::Font / vsg::GlyphMetrics). The canonical way of
    producing one is vsgXchange, but the installed vsgXchange is ABI
    incompatible with the vsg version used here (see R15), hence this small
    freetype based builder.

    The glyphs are rasterized with freetype's antialiased coverage and packed
    into a regular grid atlas.

    \warning The atlas stores the **coverage**, not a true signed distance
    field: VSG's text shader applies a smoothstep() around 0.5, so the result
    is readable but the edges are slightly softer than a real SDF. Swapping in
    a proper SDF generator only requires changing the rasterization step.

    \warning Only the characters of the requested range are available. The
    default range is printable ASCII; CJK labels require a much larger atlas
    and are not covered yet (TODO(M5.2)).
**/
class ccVSGFontBuilder
{
  public:
	//! Character range used by default (printable ASCII)
	static constexpr uint32_t DefaultFirstChar = 32;
	static constexpr uint32_t DefaultLastChar  = 126;

	//! Builds a font from the given file
	/** \param fontFile path of a .ttf / .otf / .ttc file
	    \param firstChar,lastChar inclusive character range to rasterize
	    \param pixelHeight rasterization height, in pixels
	    \return the font, or null on failure
	 **/
	static vsg::ref_ptr<vsg::Font> buildFont(const QString& fontFile,
	                                         uint32_t       firstChar   = DefaultFirstChar,
	                                         uint32_t       lastChar    = DefaultLastChar,
	                                         uint32_t       pixelHeight = 32);

	//! Returns a sensible default font file for the current platform
	/** Prefers a font that also covers CJK so that the range can be extended
	    later without changing the file. **/
	static QString defaultFontFile();
};

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
#include "qCC_renderCore.h"

// system
#include <cstddef>
#include <cstdint>

//! Rendering pass
/** Mirrors the historical CloudCompare passes: background, 3D scene, then the
	2D overlay. An additional pass is used for offscreen (color based or
	integer based) picking.
**/
enum class ccRenderPass
{
	Background,
	Scene3D,
	Overlay2D,
	Picking,
};

//! Kind of primitive to draw
enum class ccPrimitiveKind
{
	Points,
	Lines,
	LineStrip,
	Triangles,
};

//! A batch of non indexed vertices
struct ccVertexBatch
{
	//! xyz triplets (may be nullptr when using the current color only)
	const float* positions = nullptr;
	//! byte stride between two positions (0 = tightly packed)
	std::size_t positionStride = 0;
	//! rgba quadruplets (8 bits per component, may be nullptr)
	const std::uint8_t* colors = nullptr;
	//! byte stride between two colors (0 = tightly packed)
	std::size_t colorStride = 0;
	//! number of vertices
	std::size_t count = 0;
};

//! A batch of indexed vertices
struct ccIndexedBatch
{
	const float* positions    = nullptr;
	std::size_t  positionStride = 0;
	const float* normals      = nullptr;
	std::size_t  normalStride   = 0;
	const std::uint8_t* colors = nullptr;
	std::size_t  colorStride    = 0;
	const std::uint32_t* indices = nullptr;
	//! number of vertices
	std::size_t vertexCount = 0;
	//! number of indices
	std::size_t indexCount  = 0;
};

//! Backend agnostic rendering command sink
/** Transitional interface: the historical ccHObject::drawMeOnly() methods emit
	raw OpenGL calls. During the migration they can instead emit backend
	agnostic commands through this sink, which is implemented by both the
	OpenGL and the VSG backends.

	\note The VSG backend will ultimately rely on a scene graph mirror
	(see ccVSGSceneBuilder) for the standard entities; this sink remains the
	extension point used by plugins providing their own drawables
	(e.g. qSRA, qCompass).
**/
class CC_RENDER_CORE_LIB_API ccRenderCommandSink
{
  public:
	virtual ~ccRenderCommandSink() = default;

	//! Starts a new pass
	virtual void beginPass(ccRenderPass pass) = 0;

	//! Ends the current pass
	virtual void endPass() = 0;

	//! Sets the current model transformation (column major 4x4 matrix)
	virtual void setTransform(const double* mat4x4) = 0;

	//! Sets the current (uniform) color
	virtual void setColor(float r, float g, float b, float a) = 0;

	//! Sets the current point size (in pixels)
	virtual void setPointSize(float size) = 0;

	//! Sets the current line width (in pixels)
	virtual void setLineWidth(float width) = 0;

	//! Draws a non indexed batch
	virtual void draw(ccPrimitiveKind kind, const ccVertexBatch& batch) = 0;

	//! Draws an indexed batch
	virtual void drawIndexed(ccPrimitiveKind kind, const ccIndexedBatch& batch) = 0;
};

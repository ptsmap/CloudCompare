#pragma once
// ##########################################################################
// #                                                                        #
// #                              CLOUDCOMPARE                              #
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
// #          COPYRIGHT: EDF R&D / TELECOM ParisTech (ENST-TSI)             #
// #                                                                        #
// ##########################################################################

#include "qCC_glWindow.h"

// qCC_db
#include <ccIncludeGL.h> //Always first!

// qCC_renderCore
#include <ccViewInterface.h>

// Qt
#include <QImage>

// NOTE: CC_VIEW_ORIENTATION now lives in ccViewInterface.h (M8 / D.18.5) so
// that ccViewInterface::setView() can be declared for every render backend.

class CCGLWINDOW_LIB_API ccGLUtils
{
  public:
	/***************************************************
	                OpenGL Textures
	***************************************************/

	enum TextureArea
	{
		Full,
		HalfLeft,
		HalfRight
	};

	static void        DisplayTexture2DPosition(QImage image, int x, int y, int w, int h, unsigned char alpha = 255, TextureArea area = Full);
	inline static void DisplayTexture2D(QImage image, int w, int h, unsigned char alpha = 255)
	{
		DisplayTexture2DPosition(image, -w / 2, -h / 2, w, h, alpha);
	}

	static void        DisplayTexture2DPosition(GLuint texID, int x, int y, int w, int h, unsigned char alpha = 255, TextureArea area = Full);
	inline static void DisplayTexture2D(GLuint texID, int w, int h, unsigned char alpha = 255)
	{
		DisplayTexture2DPosition(texID, -w / 2, -h / 2, w, h, alpha);
	}

	/***************************************************
	                OpenGL Matrices
	***************************************************/

	//! Returns a 4x4 'OpenGL' matrix corresponding to a default 'view' orientation
	/** \param orientation view orientation
	    \param vertDir vertical direction
	    \param[out] _vertAngle_rad rotation angle about the vertical direction
	    \param[out] _orthoAngle_rad rotation angle about a direction orhogonal to the vertical one
	    \return corresponding GL matrix
	**/
	static ccGLMatrixd GenerateViewMat(CC_VIEW_ORIENTATION orientation,
	                                   const CCVector3d&   vertDir         = CCVector3d(0, 0, 1),
	                                   double*             _vertAngle_rad  = nullptr,
	                                   double*             _orthoAngle_rad = nullptr);

	//! Returns a 4x4 'OpenGL' matrix from a vertical orientation and 2 angles
	/** The 2 angles represent the rotation about the vertical direction and another rotation about
	    a direction orhogonal to the vertical one
	    \param vertDir vertical direction
	    \return corresponding GL matrix
	**/
	static ccGLMatrixd GenerateViewMat(const CCVector3d& vertDir,
	                                   double            vertAngle_rad,
	                                   double            orthoAngle_rad);
};

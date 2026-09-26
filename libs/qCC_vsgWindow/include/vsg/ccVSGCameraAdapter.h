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
#include <vsg/app/ProjectionMatrix.h>
#include <vsg/app/ViewMatrix.h>
#include <vsg/maths/mat4.h>

class ccGLMatrixd;

//! View matrix fed directly by the CloudCompare camera parameters
/** Instead of using vsg::LookAt (eye/center/up) we expose the view matrix
    computed by ccViewportParameters::computeViewMatrix(), so that the two
    backends share exactly the same camera model (pivot point, object or
    viewer centered view, focal distance).
**/
class CCVSGWINDOW_LIB_API ccVSGViewMatrix : public vsg::Inherit<vsg::ViewMatrix, ccVSGViewMatrix>
{
  public:
	vsg::dmat4 matrix;

	vsg::dmat4 transform(const vsg::dvec3& offset = {}) const override;
};

//! Projection matrix fed directly by the CloudCompare projection computation
/** The matrix stored here must be a **Vulkan** projection matrix:
    - VSG uses **reverse depth**: near plane maps to NDC z = 1, far plane to 0
      (OpenGL maps them to -1 and +1 respectively)
    - the Y axis is inverted (Vulkan clip space Y points down)

    Both differences are handled by building the matrix with the vsg::
    perspective()/orthographic() helpers, which already produce reverse depth
    matrices. Anything that reads back depth (M6: point picking, unprojection)
    must therefore convert with `z_ndc` in [1..0] and NOT with [-1..1].
**/
class CCVSGWINDOW_LIB_API ccVSGProjectionMatrix : public vsg::Inherit<vsg::ProjectionMatrix, ccVSGProjectionMatrix>
{
  public:
	vsg::dmat4 matrix;

	vsg::dmat4 transform() const override;
	vsg::dmat4 inverse() const override;

	//! Extent changes are handled by ccVSGWindowInterface::updateCamera()
	void changeExtent(const VkExtent2D& /*prev*/, const VkExtent2D& /*next*/) override {}
};

//! Converts a CloudCompare (OpenGL, column major) matrix to a VSG one
vsg::dmat4 toVSGMatrix(const ccGLMatrixd& mat);

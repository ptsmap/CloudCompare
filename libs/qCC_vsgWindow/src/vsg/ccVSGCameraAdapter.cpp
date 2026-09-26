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
#include <vsg/ccVSGCameraAdapter.h>

// qCC_db
#include <ccGLMatrix.h>

vsg::dmat4 ccVSGViewMatrix::transform(const vsg::dvec3& /*offset*/) const
{
	return matrix;
}

vsg::dmat4 ccVSGProjectionMatrix::transform() const
{
	return matrix;
}

vsg::dmat4 ccVSGProjectionMatrix::inverse() const
{
	return vsg::inverse(matrix);
}

vsg::dmat4 toVSGMatrix(const ccGLMatrixd& mat)
{
	const double* m = mat.data();

	// Both are column major, so the values are simply copied as is:
	//   - ccGLMatrixd : data[col * 4 + row], like OpenGL
	//   - vsg::t_mat4 : operator()(c, r) -> value[c][r], and its 16 scalar
	//                   constructor fills column 0, then column 1, ...
	//
	// /!\ Do NOT transpose here: transposing silently breaks the camera (the
	// translation of the view matrix ends up in the wrong column, so any
	// pivot point / camera center is ignored, and the rotations are reversed).
	return vsg::dmat4(m[0], m[1], m[2], m[3],
	                  m[4], m[5], m[6], m[7],
	                  m[8], m[9], m[10], m[11],
	                  m[12], m[13], m[14], m[15]);
}

ccGLMatrixd fromVSGMatrix(const vsg::dmat4& mat)
{
	ccGLMatrixd result;

	// same remark as above: both are column major, no transposition
	double* m = result.data();
	for (int col = 0; col < 4; ++col)
	{
		for (int row = 0; row < 4; ++row)
		{
			m[col * 4 + row] = mat[col][row];
		}
	}

	return result;
}

vsg::dmat4 vulkanToGLProjection(const vsg::dmat4& vulkanProj)
{
	// VSG renders with a **reverse depth** projection (the near plane maps to
	// NDC z = 1 and the far plane to 0, while OpenGL uses -1 and +1) and with
	// an inverted Y axis (the Vulkan clip space Y points down).
	//
	// Converting the clip coordinates is enough to get back an OpenGL matrix:
	//   y_gl = -y_vk
	//   z_gl = 1 - 2 * z_vk   (i.e. z_clip_gl = -2 * z_clip_vk + w)
	//
	// which, for a row based description of the matrices, gives:
	//   row0_gl =  row0_vk
	//   row1_gl = -row1_vk
	//   row2_gl = -2 * row2_vk + row3_vk
	//   row3_gl =  row3_vk
	//
	// This holds both for perspective and orthographic matrices.
	const vsg::dmat4 m(1.0, 0.0, 0.0, 0.0,
	                   0.0, -1.0, 0.0, 0.0,
	                   0.0, 0.0, -2.0, 0.0,
	                   0.0, 0.0, 1.0, 1.0);

	return m * vulkanProj;
}

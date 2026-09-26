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

	// ccGLMatrixd is stored like OpenGL: column major, i.e. m[col * 4 + row].
	// VSG matrices are constructed row by row.
	return vsg::dmat4(m[0], m[4], m[8], m[12],
	                  m[1], m[5], m[9], m[13],
	                  m[2], m[6], m[10], m[14],
	                  m[3], m[7], m[11], m[15]);
}

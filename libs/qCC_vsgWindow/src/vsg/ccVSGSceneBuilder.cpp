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
#include <vsg/ccVSGSceneBuilder.h>

// qCC_db
#include <ccHObject.h>
#include <ccPointCloud.h>
#include <ccGLMatrix.h>

// VSG
#include <vsg/all.h>

namespace
{
	vsg::dmat4 toVSG(const ccGLMatrix& mat)
	{
		const float* m = mat.data();

		// ccGLMatrix is column major (OpenGL), VSG matrices are built row by row
		return vsg::dmat4(m[0], m[4], m[8], m[12],
		                  m[1], m[5], m[9], m[13],
		                  m[2], m[6], m[10], m[14],
		                  m[3], m[7], m[11], m[15]);
	}
} // namespace

void ccVSGSceneBuilder::setRoot(ccHObject* root)
{
	if (m_root == root)
	{
		return;
	}

	m_root = root;
	invalidate();
}

void ccVSGSceneBuilder::invalidate()
{
	m_dirty = true;
}

void ccVSGSceneBuilder::update()
{
	if (!m_dirty)
	{
		return;
	}

	rebuild();
	m_dirty = false;
}

void ccVSGSceneBuilder::rebuild()
{
	if (!m_sceneRoot)
	{
		m_sceneRoot = vsg::Group::create();
	}

	// keep the very same root node: only its content changes
	m_sceneRoot->children.clear();
	m_entries.clear();

	if (m_root)
	{
		addChildren(m_root, m_sceneRoot);
	}
}

void ccVSGSceneBuilder::addChildren(ccHObject* parent, vsg::ref_ptr<vsg::Group> parentGroup)
{
	if (!parent || !parentGroup)
	{
		return;
	}

	const unsigned childCount = parent->getChildrenNumber();

	for (unsigned i = 0; i < childCount; ++i)
	{
		ccHObject* child = parent->getChild(i);

		if (!child || !child->isEnabled() || !child->isVisible())
		{
			continue;
		}

		auto group = vsg::Group::create();

		// optional per entity transformation
		if (child->isGLTransEnabled())
		{
			auto transform = vsg::MatrixTransform::create();
			transform->matrix = toVSG(child->getGLTransformation());
			group->addChild(transform);
			parentGroup->addChild(group);
			m_entries[child] = group;
			addChildren(child, group);
			continue;
		}

		// entity content
		// TODO(M3): meshes, polylines, sensors, ...
		if (child->isKindOf(CC_TYPES::POINT_CLOUD))
		{
			auto* cloud = static_cast<ccPointCloud*>(child);

			vsg::ref_ptr<vsg::Node> node = m_pointCloudBuilder.build(cloud, ccColor::white);
			if (node)
			{
				group->addChild(node);
			}
		}

		parentGroup->addChild(group);
		m_entries[child] = group;

		addChildren(child, group);
	}
}

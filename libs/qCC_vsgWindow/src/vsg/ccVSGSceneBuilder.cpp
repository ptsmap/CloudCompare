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
#include <ccGLMatrix.h>
#include <ccGenericMesh.h>
#include <ccHObject.h>
#include <ccPointCloud.h>
#include <ccPolyline.h>
#include <ccScalarField.h>

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

	constexpr quint64 HashPrime = 1000003;

	inline void hashCombine(quint64& hash, quint64 value)
	{
		hash = hash * HashPrime + value;
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

bool ccVSGSceneBuilder::update()
{
	if (!m_sceneRoot)
	{
		m_sceneRoot = vsg::Group::create();
	}

	if (m_dirty)
	{
		rebuild();
		m_dirty = false;
		return true;
	}

	return syncIncremental();
}

void ccVSGSceneBuilder::rebuild()
{
	m_sceneRoot->children.clear();
	m_entries.clear();

	if (!m_root)
	{
		return;
	}

	std::set<ccHObject*> visited;
	syncChildren(m_root, m_sceneRoot, visited);
}

bool ccVSGSceneBuilder::syncIncremental()
{
	bool changed = false;

	std::set<ccHObject*> visited;

	if (m_root)
	{
		changed = syncChildren(m_root, m_sceneRoot, visited) || changed;
	}

	// drop the entities that are no longer in the tree
	for (auto it = m_entries.begin(); it != m_entries.end();)
	{
		if (visited.find(it->first) == visited.end())
		{
			it = m_entries.erase(it);
			changed = true;
		}
		else
		{
			++it;
		}
	}

	return changed;
}

bool ccVSGSceneBuilder::syncChildren(ccHObject* parent,
                                    vsg::ref_ptr<vsg::Group> parentGroup,
                                    std::set<ccHObject*>&     visited)
{
	if (!parent || !parentGroup)
	{
		return false;
	}

	bool changed = false;

	const unsigned childCount = parent->getChildrenNumber();

	for (unsigned i = 0; i < childCount; ++i)
	{
		ccHObject* child = parent->getChild(i);

		if (!child || !child->isEnabled() || !child->isVisible())
		{
			continue;
		}

		visited.insert(child);

		// create the sub tree root on first sight
		auto it    = m_entries.find(child);
		bool isNew = (it == m_entries.end());

		if (isNew)
		{
			Entry entry;
			entry.node = child->isGLTransEnabled() ? vsg::ref_ptr<vsg::Node>(vsg::MatrixTransform::create())
			                                        : vsg::ref_ptr<vsg::Node>(vsg::Group::create());
			it         = m_entries.emplace(child, entry).first;
			changed    = true;
		}

		Entry& entry = it->second;

		if (syncEntity(child, entry))
		{
			changed = true;
		}

		// the children are attached to the sub tree root
		if (auto* group = dynamic_cast<vsg::Group*>(entry.node.get()))
		{
			if (syncChildren(child, vsg::ref_ptr<vsg::Group>(group), visited))
			{
				changed = true;
			}
		}
	}

	return changed;
}

bool ccVSGSceneBuilder::syncEntity(ccHObject* obj, Entry& entry)
{
	const quint64 signature = computeSignature(obj);

	if (signature == entry.signature)
	{
		return false;
	}

	// the entity changed: rebuild its content
	auto* group = dynamic_cast<vsg::Group*>(entry.node.get());
	if (!group)
	{
		return false;
	}

	// drop the previous content but keep the children groups: they are
	// re-attached by syncChildren() through the 'visited' bookkeeping.
	// (simplest correct approach for now: drop everything, children are
	//  re-created on the next sync pass)
	group->children.clear();

	if (auto* transform = dynamic_cast<vsg::MatrixTransform*>(group))
	{
		transform->matrix = toVSG(obj->getGLTransformation());
	}

	vsg::ref_ptr<vsg::Node> content;

	if (obj->isKindOf(CC_TYPES::POINT_CLOUD))
	{
		content = m_pointCloudBuilder.build(static_cast<ccPointCloud*>(obj), ccColor::white);
	}
	else if (obj->isKindOf(CC_TYPES::MESH))
	{
		content = m_meshBuilder.buildMesh(static_cast<ccGenericMesh*>(obj), ccColor::white);
	}
	else if (obj->isKindOf(CC_TYPES::POLY_LINE))
	{
		content = m_meshBuilder.buildPolyline(static_cast<ccPolyline*>(obj), ccColor::white);
	}
	// TODO(M4): sensors (ccGBLSensor / ccCameraSensor), facets, images, labels

	if (content)
	{
		group->addChild(content);
	}

	entry.signature = signature;

	return true;
}

quint64 ccVSGSceneBuilder::computeSignature(ccHObject* obj)
{
	quint64 hash = 17;

	hashCombine(hash, obj->isEnabled() ? 1u : 2u);
	hashCombine(hash, obj->isVisible() ? 3u : 5u);
	hashCombine(hash, obj->isGLTransEnabled() ? 7u : 11u);

	if (obj->isGLTransEnabled())
	{
		const float* m = obj->getGLTransformation().data();
		for (unsigned i = 0; i < 16; ++i)
		{
			hashCombine(hash, static_cast<quint64>(m[i] * 1.0e6f));
		}
	}

	if (obj->isKindOf(CC_TYPES::POINT_CLOUD))
	{
		auto* cloud = static_cast<ccPointCloud*>(obj);

		hashCombine(hash, cloud->size());
		hashCombine(hash, cloud->hasColors() ? 13u : 17u);
		hashCombine(hash, cloud->hasNormals() ? 19u : 23u);

		ccScalarField* sf = cloud->getCurrentDisplayedScalarField();
		hashCombine(hash, reinterpret_cast<quintptr>(sf));

		if (sf)
		{
			hashCombine(hash, reinterpret_cast<quintptr>(sf->getColorScale().get()));
			hashCombine(hash, sf->getColorRampSteps());
			hashCombine(hash, static_cast<quint64>(sf->displayRange().min() * 1.0e6));
			hashCombine(hash, static_cast<quint64>(sf->displayRange().max() * 1.0e6));
		}
	}
	else if (obj->isKindOf(CC_TYPES::MESH))
	{
		auto* mesh = static_cast<ccGenericMesh*>(obj);

		hashCombine(hash, mesh->size());
		hashCombine(hash, mesh->hasTriNormals() ? 29u : 31u);

		if (ccGenericPointCloud* cloud = mesh->getAssociatedCloud())
		{
			hashCombine(hash, cloud->size());
			hashCombine(hash, cloud->hasColors() ? 37u : 41u);
		}
	}
	else if (obj->isKindOf(CC_TYPES::POLY_LINE))
	{
		auto* poly = static_cast<ccPolyline*>(obj);

		hashCombine(hash, poly->size());
		hashCombine(hash, poly->isClosed() ? 43u : 47u);
		hashCombine(hash, poly->hasColors() ? 53u : 59u);
	}

	return hash;
}

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

// qCC_glWindow (only for ccGui::Parameters(): the persistent display params)
#include <ccGuiParameters.h>

// qCC_db
#include <ccGLMatrix.h>
#include <ccGenericMesh.h>
#include <ccHObject.h>
#include <ccPointCloud.h>
#include <ccPolyline.h>
#include <ccScalarField.h>
#include <ccSensor.h>

// VSG
#include <vsg/all.h>

// system
#include <cmath>

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

	//! Bounding sphere of an entity, used by the depth sorted (transparent) nodes
	vsg::dsphere computeObjectBound(ccHObject* obj)
	{
		const ccBBox    bb   = obj->getOwnBB(true);
		const CCVector3 minC = bb.minCorner();
		const CCVector3 maxC = bb.maxCorner();

		const vsg::dvec3 center((minC.x + maxC.x) * 0.5,
		                        (minC.y + maxC.y) * 0.5,
		                        (minC.z + maxC.z) * 0.5);

		const double dx = static_cast<double>(maxC.x) - static_cast<double>(minC.x);
		const double dy = static_cast<double>(maxC.y) - static_cast<double>(minC.y);
		const double dz = static_cast<double>(maxC.z) - static_cast<double>(minC.z);

		const double radius = 0.5 * std::sqrt(dx * dx + dy * dy + dz * dz);

		return vsg::dsphere(center, std::max(radius, 1.0));
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

	if (!m_idSceneRoot)
	{
		m_idSceneRoot = vsg::Group::create();
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
	m_idSceneRoot->children.clear();
	m_entries.clear();
	m_idToEntity.clear();

	if (!m_root)
	{
		return;
	}

	std::set<ccHObject*> visited;
	syncChildren(m_root, m_sceneRoot, m_idSceneRoot, visited);
}

ccHObject* ccVSGSceneBuilder::entityForId(uint32_t id) const
{
	// 0 is the background: the picking attachment is cleared with it
	if (id == 0)
	{
		return nullptr;
	}

	const auto it = m_idToEntity.find(id);

	return (it != m_idToEntity.end()) ? it->second : nullptr;
}

bool ccVSGSceneBuilder::syncIncremental()
{
	bool changed = false;

	std::set<ccHObject*> visited;

	if (m_root)
	{
		changed = syncChildren(m_root, m_sceneRoot, m_idSceneRoot, visited) || changed;
	}

	// drop the entities that are no longer in the tree
	for (auto it = m_entries.begin(); it != m_entries.end();)
	{
		if (visited.find(it->first) == visited.end())
		{
			// ... and forget the ID it used to render (M6.1)
			m_idToEntity.erase(static_cast<uint32_t>(it->first->getUniqueID()));
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

bool ccVSGSceneBuilder::syncChildren(ccHObject*               parent,
                                    vsg::ref_ptr<vsg::Group> parentGroup,
                                    vsg::ref_ptr<vsg::Group> parentIdGroup,
                                    std::set<ccHObject*>&    visited)
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

		// mirrors ccHObject::draw(): a disabled entity is skipped entirely,
		// but an invisible one still has its *children* processed - only its
		// own content is not drawn (see the children loop of ccHObject::draw,
		// which recurses unconditionally)
		if (!child || !child->isEnabled())
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
			// mirror sub tree root of the picking scene graph (M6.1): same
			// hierarchy and same transformations as the display one
			entry.idNode = child->isGLTransEnabled() ? vsg::ref_ptr<vsg::Node>(vsg::MatrixTransform::create())
			                                          : vsg::ref_ptr<vsg::Node>(vsg::Group::create());
			it         = m_entries.emplace(child, entry).first;
			// mount the sub tree root onto the parent group - this is what
			// actually inserts the entity into the VSG scene graph
			parentGroup->addChild(entry.node);
			if (parentIdGroup)
			{
				parentIdGroup->addChild(entry.idNode);
			}
			changed    = true;
		}

		Entry& entry = it->second;

		// the entity's own content is only built when it is visible or
		// selected (a freshly loaded entity is selected but not yet visible)
		if ((child->isVisible() || child->isSelected()) && syncEntity(child, entry))
		{
			changed = true;
		}

		// the children are attached to the sub tree root
		if (auto* group = dynamic_cast<vsg::Group*>(entry.node.get()))
		{
			auto* idGroup = dynamic_cast<vsg::Group*>(entry.idNode.get());

			if (syncChildren(child,
			                 vsg::ref_ptr<vsg::Group>(group),
			                 vsg::ref_ptr<vsg::Group>(idGroup),
			                 visited))
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

	// the picking sub tree mirrors the display one (M6.1)
	auto* idGroup = dynamic_cast<vsg::Group*>(entry.idNode.get());
	if (idGroup)
	{
		idGroup->children.clear();

		if (auto* idTransform = dynamic_cast<vsg::MatrixTransform*>(idGroup))
		{
			idTransform->matrix = toVSG(obj->getGLTransformation());
		}
	}

	ccVSGBuiltNodes built;
	bool            transparent = false;

	// the ID the picking pass writes for this entity: the CloudCompare unique
	// ID, so that the result can be handed to ccPickingHub as-is (see M6.1)
	const uint32_t entityId = static_cast<uint32_t>(obj->getUniqueID());

	if (obj->isKindOf(CC_TYPES::POINT_CLOUD))
	{
		built = m_pointCloudBuilder.build(static_cast<ccPointCloud*>(obj), ccColor::white, entityId);
	}
	else if (obj->isKindOf(CC_TYPES::MESH))
	{
		built = m_meshBuilder.buildMesh(static_cast<ccGenericMesh*>(obj), ccColor::white, entityId, &transparent);
	}
	else if (obj->isKindOf(CC_TYPES::POLY_LINE))
	{
		built = m_meshBuilder.buildPolyline(static_cast<ccPolyline*>(obj), ccColor::white, entityId, &transparent);
	}
	else if (obj->isKindOf(CC_TYPES::SENSOR))
	{
		built = m_meshBuilder.buildSensor(static_cast<ccSensor*>(obj), entityId);
	}
	// TODO(M4): facets, images, labels

	vsg::ref_ptr<vsg::Node> content = built.display;

	// Transparent entities are collected in a dedicated bin and sorted back to
	// front by the view (M4.5). The matching vsg::Bin is registered by
	// ccVSGWindowInterface::initializeViewer().
	//
	// NOTE: the picking pass needs no sorting at all, so the ID node is always
	// mounted as-is.
	if (transparent && content)
	{
		content = vsg::DepthSorted::create(CC_VSG_TRANSPARENT_BIN, computeObjectBound(obj), content);
	}

	if (content)
	{
		group->addChild(content);
	}

	if (built.ids && idGroup)
	{
		idGroup->addChild(built.ids);
		m_idToEntity[entityId] = obj;
	}
	else
	{
		// not pickable (or no ID sub tree): make sure a stale entry is dropped
		m_idToEntity.erase(entityId);
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
		// M4.6: the double sided lighting flag changes the mesh shader (and the
		// back face culling), so a toggle must force the mesh to be rebuilt
		hashCombine(hash, ccGui::Parameters().lightDoubleSided ? 61u : 67u);

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
		// the width drives the quad expansion (M4)
		hashCombine(hash, static_cast<quint64>(poly->getWidth() * 1.0e6f));
	}
	else if (obj->isKindOf(CC_TYPES::SENSOR))
	{
		auto* sensor = static_cast<ccSensor*>(obj);

		hashCombine(hash, static_cast<quint64>(sensor->getGraphicScale() * 1.0e6));
		hashCombine(hash, static_cast<quint64>(sensor->getActiveIndex() * 1.0e6));

		ccIndexedTransformation trans;
		if (sensor->getActiveAbsoluteTransformation(trans))
		{
			const float* m = trans.data();
			for (unsigned i = 0; i < 16; ++i)
			{
				hashCombine(hash, static_cast<quint64>(m[i] * 1.0e6f));
			}
		}
	}

	return hash;
}

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
// #                    COPYRIGHT: CloudCompare project                     #
// #                                                                        #
// ##########################################################################

#include "../include/ccPickingHub.h"

// Local
#include "../include/ccMainAppInterface.h"

// qCC_db
#include <ccSphere.h>

// qCC_renderCore
#include <ccViewInterface.h>

// Qt
#include <QMdiSubWindow>
#include <QMessageBox>

ccPickingHub::ccPickingHub(ccMainAppInterface* app, QObject* parent /*=nullptr*/)
    : QObject(parent)
    , m_app(app)
    , m_pickingMode(ccViewInterface::POINT_OR_TRIANGLE_PICKING)
    , m_autoEnableOnActivatedWindow(true)
    , m_exclusive(false)
    , m_activeView(nullptr)
{
}

// void ccPickingHub::setPickingMode(ccGLWindowInterface::PICKING_MODE mode, bool autoEnableOnActivatedWindow/*=true*/)
//{
//	m_pickingMode = mode;
//	m_autoEnableOnActivatedWindow = autoEnableOnActivatedWindow;
// }

void ccPickingHub::togglePickingMode(bool state)
{
	// ccLog::Warning(QString("Toggle picking mode: ") + (state ? "ON" : "OFF") + " --> " + (m_activeGLWindow ? QString("View ") + QString::number(m_activeGLWindow->getUniqueID()) : QString("no view")));
	if (m_activeView)
	{
		m_activeView->setPickingMode(state ? m_pickingMode : ccViewInterface::DEFAULT_PICKING);
	}
}

void ccPickingHub::onActiveWindowChanged(QMdiSubWindow* mdiSubWindow)
{
	// backend agnostic: works for both ccGLWindow and ccVSGWindow
	ccViewInterface* view = (mdiSubWindow ? ccViewInterface::FromWidget(mdiSubWindow->widget()) : nullptr);

	if (m_activeView == view)
	{
		// nothing to do
		return;
	}

	if (m_activeView)
	{
		// take care of the previously linked view
		togglePickingMode(false);
		QObject::disconnect(m_activeView->signalEmitter(), nullptr, this, nullptr);
		m_activeView = nullptr;
	}

	if (view)
	{
		// link this new view. Both backends expose their signals through a
		// ccViewSignalEmitter (see qCC_db).
		ccViewSignalEmitter* emitter = qobject_cast<ccViewSignalEmitter*>(view->signalEmitter());
		if (emitter)
		{
			connect(emitter, &ccViewSignalEmitter::itemPicked, this, &ccPickingHub::processPickedItem, Qt::UniqueConnection);
		}

		m_activeView = view;

		if (m_autoEnableOnActivatedWindow && !m_listeners.empty())
		{
			togglePickingMode(true);
		}
	}
}

void ccPickingHub::onActiveWindowDeleted(ccViewInterface* view)
{
	if (m_activeView && view == m_activeView)
	{
		m_activeView = nullptr;
	}
}

void ccPickingHub::processPickedItem(ccHObject* entity, unsigned itemIndex, int x, int y, const CCVector3& P3D, const CCVector3d& uvw)
{
	if (m_listeners.empty())
	{
		return;
	}

	ccPickingListener::PickedItem item;
	{
		item.clickPoint = QPoint(x, y);
		item.entity     = entity;
		item.itemIndex  = itemIndex;
		item.P3D        = P3D;
		item.uvw        = uvw;

		if (entity && entity->isA(CC_TYPES::SPHERE))
		{
			// whether the center of sphere entities should be used when a point is picked on the surface
			static QMessageBox::StandardButton s_pickSphereCenter = QMessageBox::Yes;

			if (s_pickSphereCenter != QMessageBox::YesToAll && s_pickSphereCenter != QMessageBox::NoToAll)
			{
				s_pickSphereCenter = QMessageBox::question(m_activeView->asWidget(), tr("Sphere picking"), tr("From now on, do you want to pick sphere centers instead of a point on their surface?"), QMessageBox::YesToAll | QMessageBox::Yes | QMessageBox::No | QMessageBox::NoToAll, QMessageBox::YesToAll);
			}
			if (s_pickSphereCenter == QMessageBox::Yes || s_pickSphereCenter == QMessageBox::YesToAll)
			{
				// replace the input point by the sphere center
				item.P3D          = static_cast<ccSphere*>(entity)->getOwnBB().getCenter();
				item.entityCenter = true;
			}
		}
	}

	// copy the list of listeners, in case the user call 'removeListener' in 'onItemPicked'
	std::set<ccPickingListener*> listeners = m_listeners;
	for (ccPickingListener* listener : listeners)
	{
		if (listener)
		{
			listener->onItemPicked(item);
		}
	}
}

bool ccPickingHub::addListener(ccPickingListener*            listener,
                               bool                          exclusive /*=false*/,
                               bool                          autoStartPicking /*=true*/,
                               ccViewInterface::PICKING_MODE mode /*=ccViewInterface::POINT_OR_TRIANGLE_PICKING*/)
{
	if (!listener)
	{
		assert(false);
		return false;
	}

	// if listeners are already registered
	if (!m_listeners.empty())
	{
		if (m_exclusive) // a previous listener is exclusive
		{
			assert(m_listeners.size() == 1);
			if (m_listeners.find(listener) == m_listeners.end())
			{
				ccLog::Warning("[ccPickingHub::addListener] Exclusive listener already registered: stop the other tool relying on point picking first");
				return false;
			}
		}
		else if (exclusive) // this new listener is exclusive
		{
			if (m_listeners.size() > 1 || m_listeners.find(listener) == m_listeners.end())
			{
				ccLog::Warning("[ccPickingHub::addListener] Attempt to register an exclusive listener while other listeners are already registered");
				return false;
			}
		}
		else if (mode != m_pickingMode)
		{
			if (m_listeners.size() > 1 || m_listeners.find(listener) == m_listeners.end())
			{
				ccLog::Warning("[ccPickingHub::addListener] Other listeners are already registered with a different picking mode");
				return false;
			}
		}
	}

	try
	{
		m_listeners.insert(listener);
	}
	catch (const std::bad_alloc&)
	{
		// not enough memory
		ccLog::Warning("[ccPickingHub::addListener] Not enough memory");
		return false;
	}

	m_exclusive   = exclusive;
	m_pickingMode = mode;

	if (autoStartPicking)
	{
		togglePickingMode(true);
	}

	return true;
}

void ccPickingHub::removeListener(ccPickingListener* listener, bool autoStopPickingIfLast /*=true*/)
{
	m_listeners.erase(listener);

	if (m_listeners.empty())
	{
		m_exclusive = false; // auto drop the 'exclusive' flag
		togglePickingMode(false);
	}
}

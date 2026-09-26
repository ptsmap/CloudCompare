//##########################################################################
//#                                                                        #
//#                CLOUDCOMPARE PLUGIN: ExamplePlugin                      #
//#                                                                        #
//#  This program is free software; you can redistribute it and/or modify  #
//#  it under the terms of the GNU General Public License as published by  #
//#  the Free Software Foundation; version 2 of the License.               #
//#                                                                        #
//#  This program is distributed in the hope that it will be useful,       #
//#  but WITHOUT ANY WARRANTY; without even the implied warranty of        #
//#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         #
//#  GNU General Public License for more details.                          #
//#                                                                        #
//#                             COPYRIGHT: XXX                             #
//#                                                                        #
//##########################################################################

// First:
//	Replace all occurrences of 'ExamplePlugin' by your own plugin class name in this file.
//	This includes the resource path to info.json in the constructor.

// Second:
//	Open ExamplePlugin.qrc, change the "prefix" and the icon filename for your plugin.
//	Change the name of the file to <yourPluginName>.qrc

// Third:
//	Open the info.json file and fill in the information about the plugin.
//	 "type" should be one of: "Standard", "GL", or "I/O" (required)
//	 "name" is the name of the plugin (required)
//	 "icon" is the Qt resource path to the plugin's icon (from the .qrc file)
//	 "description" is used as a tootip if the plugin has actions and is displayed in the plugin dialog
//	 "authors", "maintainers", and "references" show up in the plugin dialog as well

#include <QtGui>

#include "ExamplePlugin.h"

#include "ActionA.h"

// Default constructor:
//	- pass the Qt resource path to the info.json file (from <yourPluginName>.qrc file) 
//  - constructor should mainly be used to initialize actions and other members
ExamplePlugin::ExamplePlugin( QObject *parent )
	: QObject( parent )
	, ccStdPluginInterface( ":/CC/plugin/ExamplePlugin/info.json" )
	, m_action( nullptr )
{
}

// This method should enable or disable your plugin actions
// depending on the currently selected entities ('selectedEntities').
void ExamplePlugin::onNewSelection( const ccHObject::Container &selectedEntities )
{
	if ( m_action )
		m_action->setEnabled( !selectedEntities.empty() );
	if ( m_pickPointAction )
		m_pickPointAction->setEnabled( m_app && m_app->pickingHub() );
}

// This method returns all the 'actions' your plugin can perform.
// getActions() will be called only once, when plugin is loaded.
QList<QAction *> ExamplePlugin::getActions()
{
	if ( !m_action )
	{
		m_action = new QAction( getName(), this );
		m_action->setToolTip( getDescription() );
		m_action->setIcon( getIcon() );
		connect( m_action, &QAction::triggered, this, [this]()
		{
			Example::performActionA( m_app );
		});
	}
	if ( !m_pickPointAction )
	{
		m_pickPointAction = new QAction( tr("Pick Point"), this );
		m_pickPointAction->setToolTip( tr("Pick a point in 3D view and print coordinates to log"));
		m_pickPointAction->setIcon( QIcon::fromTheme("crosshair") );
		connect( m_pickPointAction, &QAction::triggered, this, [this]() { startPicking(); });
	}
	return { m_action, m_pickPointAction };
}
#include <ccLog.h>
#include <ccPickingHub.h>

// 启动选点模式
void ExamplePlugin::startPicking()
{
	if ( m_picking || !m_app )
		return;
	ccPickingHub* hub = m_app->pickingHub();
	if ( !hub )
	{
		ccLog::Warning(tr("PickingHub not available!"));
		return;
	}
	if ( hub->addListener(this, false, true) )
	{
		m_picking = true;
		ccLog::Print(tr("[ExamplePlugin] Pick mode enabled. Click a point in the 3D view."));
	}
	else
	{
		ccLog::Warning(tr("Failed to register picking listener!"));
	}
}

// 停止选点模式
void ExamplePlugin::stopPicking()
{
	if ( !m_picking || !m_app )
		return;
	ccPickingHub* hub = m_app->pickingHub();
	if ( hub )
		hub->removeListener(this, true);
	m_picking = false;
	ccLog::Print(tr("[ExamplePlugin] Pick mode disabled."));
}

// 处理选点事件
void ExamplePlugin::onItemPicked(const PickedItem& pi)
{
	if ( !m_picking )
		return;
	stopPicking();
	ccLog::Print(tr("[ExamplePlugin] Picked point: (%1, %2, %3)")
				 .arg(pi.P3D.x, 0, 'g', 8)
				 .arg(pi.P3D.y, 0, 'g', 8)
				 .arg(pi.P3D.z, 0, 'g', 8));
}

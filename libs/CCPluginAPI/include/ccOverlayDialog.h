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

// Local
#include "CCPluginAPI.h"

// Qt
#include <QDialog>
#include <QList>

class ccViewInterface;
class ccGLWindowInterface;

//! Generic overlay dialog interface
class CCPLUGIN_LIB_API ccOverlayDialog : public QDialog
{
	Q_OBJECT

  public:
	//! Default constructor
	explicit ccOverlayDialog(QWidget* parent = nullptr, Qt::WindowFlags flags = Qt::FramelessWindowHint | Qt::Tool);

	//! Destructor
	~ccOverlayDialog() override;

	//! Links the overlay dialog with a MDI window
	/** Warning: link can't be modified while dialog is displayed/process is running!

	    \note It used to take a ccGLWindowInterface*, which made every
	    interactive tool OpenGL only. It now accepts any backend (M6.6).
	    \return success
	**/
	virtual bool linkWith(ccViewInterface* win);

	//! Starts process
	/** \return success
	 **/
	virtual bool start();

	//! Stops process/dialog
	/** Automatically emits the 'processFinished' signal (with input state as argument).
	    \param accepted process/dialog result
	**/
	virtual void stop(bool accepted);

	// reimplemented from QDialog
	void reject() override;

	//! Adds a keyboard shortcut (single key) that will be overridden from the associated window
	/** When an overridden key is pressed, the shortcutTriggered(int) signal is emitted.
	 **/
	void addOverriddenShortcut(Qt::Key key);

	//! Returns whether the tool is currently started or not
	bool started() const
	{
		return m_processing;
	}

  Q_SIGNALS:

	//! Signal emitted when process is finished
	/** \param accepted specifies how the process finished (accepted or not)
	 **/
	void processFinished(bool accepted);

	//! Signal emitted when an overridden key shortcut is pressed
	/** See ccOverlayDialog::addOverriddenShortcut
	 **/
	void shortcutTriggered(int key);

	//! Signal emitted when a 'show' event is detected
	void shown();

  protected:
	//! Slot called when the linked window is deleted (calls 'onClose')
	virtual void onLinkedWindowDeletion(ccViewInterface* object = nullptr);

  protected:
	// inherited from QObject
	bool eventFilter(QObject* obj, QEvent* e) override;

	//! Associated (MDI) window
	ccViewInterface* m_associatedWin;

	//! Returns m_associatedWin, downcast to the OpenGL interface
	/** Most interactive tools still rely on OpenGL only APIs (mouse grabbing,
	    toCenteredGLCoordinates(), glWidth()/glHeight(), display parameters,
	    ...), so they remain OpenGL ones for now: this returns nullptr when the
	    associated window uses another backend (M6.6). Guarding a call with it
	    is the safe way to keep such a tool working.
	 **/
	ccGLWindowInterface* glWin() const;

	//! Running/processing state
	bool m_processing;

	//! Overridden keys
	QList<int> m_overriddenKeys;
};

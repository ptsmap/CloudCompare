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

#include <ccIncludeGL.h>

// Qt
#include <QDir>
#include <QMessageBox>
#include <QPixmap>
#include <QSettings>
#include <QSplashScreen>
#include <QTime>
#include <QTimer>
#include <QTranslator>

// qCC_db
#include <ccColorScalesManager.h>
#include <ccLog.h>
#include <ccNormalVectors.h>

// qCC_io
#include <FileIOFilter.h>
#include <ccGlobalShiftManager.h>

// local
#include "ccApplication.h"

// VSG
#include <vsg/core/Exception.h>

// system
#include <exception>
#include "ccCommandLineParser.h"
#include "ccGuiParameters.h"
#include "ccPersistentSettings.h"
#include "ccTranslationManager.h"
#include "mainwindow.h"

// plugins
#include "ccPluginInterface.h"
#include "ccPluginManager.h"

// qCC_renderCore (render backend registry)
#include <ccRenderBackend.h>
#include <ccGLRenderBackend.h>

#ifdef CC_RENDER_VSG_ENABLED
// VSG render backend (see doc/VSG_Rendering_Migration_Plan.md)
#include <ccVSGWindowInterface.h>
#include <ccVSGRenderBackend.h>
#endif

#ifdef USE_VLD
#include <vld.h>
#endif

#ifdef _WIN32
#include <Windows.h>
#endif

static bool IsCommandLine(int argc, char** argv)
{
#ifdef Q_OS_MAC
	// On macOS, when double-clicking the application, the Finder (sometimes!) adds a command-line parameter
	// like "-psn_0_582385" which is a "process serial number".
	// We need to recognize this and discount it when determining if we are running on the command line or not.

	int numRealArgs = argc;

	for (int i = 1; i < argc; ++i)
	{
		if (strncmp(argv[i], "-psn_", 5) == 0)
		{
			--numRealArgs;
		}
	}

	return (numRealArgs > 1) && (argv[1][0] == '-');
#else
	return (argc > 1) && (argv[1][0] == '-');
#endif
}

int main(int argc, char** argv)
{
#ifdef _WIN32 // This will allow printf to function on windows when opened from command line
	DWORD stdout_type = GetFileType(GetStdHandle(STD_OUTPUT_HANDLE));
	if (AttachConsole(ATTACH_PARENT_PROCESS))
	{
		if (stdout_type == FILE_TYPE_UNKNOWN) // this will allow std redirection (./executable > out.txt)
		{
			freopen("CONOUT$", "w", stdout);
			freopen("CONOUT$", "w", stderr);
		}
	}
#endif

	bool commandLine = IsCommandLine(argc, argv);

	// Convert the input arguments to QString before the application is initialized
	// (as it will force utf8, which might prevent from properly reading filenames from the command line)
	QStringList argumentsLocal8Bit;
	for (int i = 0; i < argc; ++i)
	{
		argumentsLocal8Bit << QString::fromLocal8Bit(argv[i]);
	}

	// specific commands
	int lastArgumentIndex = 1;
	if (commandLine)
	{
		// translation file selection
		if (lastArgumentIndex < argumentsLocal8Bit.size()
		    && argumentsLocal8Bit[lastArgumentIndex].toUpper() == "-LANG")
		{
			// remove verified local option
			argumentsLocal8Bit.removeAt(lastArgumentIndex);

			if (lastArgumentIndex >= argumentsLocal8Bit.size())
			{
				ccLog::Error(QObject::tr("Missing argument after %1: language file").arg("-LANG"));
				return EXIT_FAILURE;
			}

			// remove verified arguments so that -SILENT will be the first one (if present)...
			QString langFilename = argumentsLocal8Bit.takeAt(lastArgumentIndex);

			ccTranslationManager::Get().loadTranslation(langFilename);
			commandLine = false;
		}

		if (lastArgumentIndex < argumentsLocal8Bit.size()
		    && argumentsLocal8Bit[lastArgumentIndex].toUpper() == "-VERBOSITY")
		{
			// remove verified local option
			argumentsLocal8Bit.removeAt(lastArgumentIndex);

			if (lastArgumentIndex >= argumentsLocal8Bit.size())
			{
				ccLog::Error(QObject::tr("Missing argument after %1: verbosity level").arg("-VERBOSITY"));
				return EXIT_FAILURE;
			}

			// remove verified arguments so that -SILENT will be the first one (if present)...
			QString verbosityLevelStr = argumentsLocal8Bit.takeAt(lastArgumentIndex);

			bool ok             = false;
			int  verbosityLevel = verbosityLevelStr.toInt(&ok);
			if (!ok || verbosityLevel < 0)
			{
				ccLog::Warning(QObject::tr("Invalid verbosity level: %1").arg(verbosityLevelStr));
			}
			else
			{
				ccLog::SetVerbosityLevel(verbosityLevel);
			}
		}
	}

	ccApplication::InitOpenGL();

	ccApplication app(argc, argv, commandLine);

	// ----------------------------------------------------------------------
	// Register the available render backends (see doc/VSG_Rendering_Migration_Plan.md).
	// The registry is a singleton, so this must happen before any 3D view is
	// created (in particular before MainWindow builds its first view).
	// ----------------------------------------------------------------------
	{
		ccRenderBackendRegistry& registry = ccRenderBackendRegistry::instance();

		registry.registerBackend(QStringLiteral("OpenGL"),
		                         []() { return std::unique_ptr<ccRenderBackend>(new ccGLRenderBackend()); });

#ifdef CC_RENDER_VSG_ENABLED
		registry.registerBackend(QStringLiteral("VSG"),
		                         []() { return std::unique_ptr<ccRenderBackend>(new ccVSGRenderBackend()); });
#endif

		// command line override: --render-backend=OpenGL|VSG (matches the
		// CC_RENDER_BACKEND environment variable handled inside the registry).
		// The flag must be STRIPPED from the argument list, otherwise
		// ccCommandLineParser rejects it as an "Unknown or misplaced command"
		// (and in GUI mode it would be mistaken for a cloud file to open).
		for (int i = argumentsLocal8Bit.size() - 1; i >= 0; --i)
		{
			const QString& arg = argumentsLocal8Bit.at(i);
			if (arg.startsWith(QStringLiteral("--render-backend=")))
			{
				const QString value = arg.mid(QStringLiteral("--render-backend=").length());
				registry.setSelectedBackendName(value);
				argumentsLocal8Bit.removeAt(i);
			}
		}

		// Recompute the mode now that our flag has been stripped: like
		// IsCommandLine() above, CloudCompare runs in command line mode only
		// when the first *real* argument starts with '-'; anything else is a
		// file to open in the GUI ('CloudCompare cloud.bin').
		// This must NOT be a plain size check: argumentsLocal8Bit still holds
		// the program name at index 0, so 'CloudCompare cloud.bin' would be
		// mistaken for the command line mode and rejected by the parser with
		// "Command expected (commands start with '-')".
		commandLine = false;
		for (int i = 1; i < argumentsLocal8Bit.size(); ++i)
		{
			// ignore the process serial number the Finder adds on macOS
			if (argumentsLocal8Bit.at(i).startsWith(QStringLiteral("-psn_")))
			{
				continue;
			}

			commandLine = argumentsLocal8Bit.at(i).startsWith('-');
			break;
		}
	}

	if (!commandLine)
	{
		// if not in CLI mode, we set the default log verbosity level
		ccLog::SetVerbosityLevel(ccGui::Parameters().logVerbosityLevel);
	}

	// store the log message until a valid logging instance is registered
	ccLog::EnableMessageBackup(true);

	// splash screen
	std::unique_ptr<QSplashScreen> splash(nullptr);

	// standard mode
	if (!commandLine)
	{
		QOpenGLContext context;
		if (!context.create())
		{
			QMessageBox::critical(nullptr, "Error", "This application needs OpenGL to run!");
			return EXIT_FAILURE;
		}

		auto* glFunc = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_2_1>(&context);
		// Check if we have at least OpenGL 2.1
		if (!glFunc)
		{
			QMessageBox::critical(nullptr, "Error", "This application needs OpenGL 2.1 at least to run!");
			return EXIT_FAILURE;
		}

		// init splash screen
		QPixmap pixmap(QString::fromUtf8(":/CC/images/imLogoV2Qt.png"));
		splash.reset(new QSplashScreen(pixmap, Qt::WindowStaysOnTopHint));
		splash->show();
	}

	// global structures initialization
	FileIOFilter::InitInternalFilters();       // load all known I/O filters (plugins will come later!)
	ccNormalVectors::GetUniqueInstance();      // force pre-computed normals array initialization
	ccColorScalesManager::GetUniqueInstance(); // force pre-computed color tables initialization

	// load the plugins
	ccPluginManager& pluginManager = ccPluginManager::Get();
	pluginManager.loadPlugins();

	// restore some global parameters
	{
		QSettings settings;
		settings.beginGroup(ccPS::GlobalShift());
		double maxAbsCoord = settings.value(ccPS::MaxAbsCoord(), ccGlobalShiftManager::MaxCoordinateAbsValue()).toDouble();
		double maxAbsDiag  = settings.value(ccPS::MaxAbsDiag(), ccGlobalShiftManager::MaxBoundgBoxDiagonal()).toDouble();
		settings.endGroup();

		ccLog::Print(QString("[Global Shift] Max abs. coord = %1 / max abs. diag = %2").arg(maxAbsCoord, 0, 'e', 0).arg(maxAbsDiag, 0, 'e', 0));

		ccGlobalShiftManager::SetMaxCoordinateAbsValue(maxAbsCoord);
		ccGlobalShiftManager::SetMaxBoundgBoxDiagonal(maxAbsDiag);
	}

	int result = 0;

	// command line mode
	if (commandLine)
	{
		// command line processing (no GUI)
		result = ccCommandLineParser::Parse(argumentsLocal8Bit, pluginManager.pluginList());
	}
	else
	{
		// main window initialization
		MainWindow* mainWindow = MainWindow::TheInstance();
		if (!mainWindow)
		{
			QMessageBox::critical(nullptr, "Error", "Failed to initialize the main application window?!");
			return EXIT_FAILURE;
		}
		mainWindow->initPlugins();
		mainWindow->show();
		QCoreApplication::processEvents();

		// show current Global Shift parameters in Console
		{
			ccLog::Print(QString("[Global Shift] Max abs. coord = %1 / max abs. diag = %2")
			                 .arg(ccGlobalShiftManager::MaxCoordinateAbsValue(), 0, 'e', 0)
			                 .arg(ccGlobalShiftManager::MaxBoundgBoxDiagonal(), 0, 'e', 0));
		}

		if (splash)
		{
			splash->close();
		}

#ifdef CC_RENDER_VSG_ENABLED
		// Debug / automated testing: create a VSG based 3D view right away.
		// See doc/VSG_Rendering_Migration_Plan.md
		if (qEnvironmentVariableIsSet("CC_VSG_VIEW"))
		{
			// Automated testing: never let a modal dialog block the event loop
			// (otherwise the screenshot step would never run). Setting very
			// large thresholds prevents the "Global Shift" dialog from showing up.
			ccGlobalShiftManager::SetMaxCoordinateAbsValue(1.0e12);
			ccGlobalShiftManager::SetMaxBoundgBoxDiagonal(1.0e12);

			// note: fprintf (and not qWarning / ccLog) so that the output really
			// ends up on stderr - CloudCompare redirects the Qt messages to its
			// own console widget
			fprintf(stderr, "[VSG] CC_VSG_VIEW is set: creating a VSG 3D view\n");
			fflush(stderr);

			// a VSG/Vulkan failure must not abort the whole application: report
			// it, so that the smoke tests (and the user) can see what happened.
			// Note: vsg::Exception is a plain struct, it does *not* derive from
			// std::exception and has to be caught by itself.
			try
			{
				mainWindow->createVSGViewDebug();
			}
			catch (const vsg::Exception& e)
			{
				fprintf(stderr, "[VSG] createVSGViewDebug failed: %s (result=%d)\n", e.message.c_str(), e.result);
			}
			catch (const std::exception& e)
			{
				fprintf(stderr, "[VSG] createVSGViewDebug failed: %s\n", e.what());
			}
			catch (...)
			{
				fprintf(stderr, "[VSG] createVSGViewDebug failed (unknown exception)\n");
			}
			fflush(stderr);

			QCoreApplication::processEvents();

			if (ccViewInterface* view = mainWindow->getActiveViewWindow())
			{
				fprintf(stderr, "[VSG] active view backend: %s\n", qPrintable(view->backendName()));
			}
			else
			{
				fprintf(stderr, "[VSG] no active view!\n");
			}
			fflush(stderr);
		}
#endif

		if (argc > lastArgumentIndex)
		{
			// any additional argument is assumed to be a filename --> we try to load it/them
			QStringList filenames;
			for (int i = lastArgumentIndex; i < argc; ++i)
			{
				QString arg = argumentsLocal8Bit[i];

				// special command: auto start a plugin
				if (arg.startsWith(":start-plugin:"))
				{
					QString pluginName      = arg.mid(14);
					QString pluginNameUpper = pluginName.toUpper();
					// look for this plugin
					bool found = false;
					for (ccPluginInterface* plugin : pluginManager.pluginList())
					{
						if (plugin->getName().replace(' ', '_').toUpper() == pluginNameUpper)
						{
							found        = true;
							bool success = plugin->start();
							if (!success)
							{
								ccLog::Error(QString("Failed to start the plugin '%1'").arg(plugin->getName()));
							}
							break;
						}
					}

					if (!found)
					{
						ccLog::Error(QString("Couldn't find the plugin '%1'").arg(pluginName.replace('_', ' ')));
					}
				}
				else
				{
					filenames << arg;
				}
			}

			mainWindow->addToDB(filenames);
		}
		fprintf(stderr, "[VSG][trace] files loaded\n");
		fflush(stderr);

#ifdef CC_RENDER_VSG_ENABLED
		// Automated testing: render the active view, save the image and quit.
		// Used by the VSG migration smoke tests (see doc/VSG_Rendering_Migration_Plan.md)
		{
			const QString screenshotPath = qEnvironmentVariable("CC_VSG_SCREENSHOT");
			if (!screenshotPath.isEmpty())
			{
				QTimer::singleShot(5000, [mainWindow, screenshotPath]()
				                   {
					                   fprintf(stderr, "[VSG] screenshot requested: %s\n", qPrintable(screenshotPath));

					                   ccViewInterface* view = mainWindow->getActiveViewWindow();
					                   fprintf(stderr, "[VSG] active view backend: %s\n",
					                           view ? qPrintable(view->backendName()) : "none");

					                   // diagnostics: is there anything in the DB / in the VSG scene graph?
					                   if (ccHObject* dbRoot = mainWindow->dbRootObject())
					                   {
						                   std::function<void(ccHObject*, int)> dump = [&](ccHObject* obj, int depth)
						                   {
							                   if (!obj)
							                   {
								                   return;
							                   }
							                   fprintf(stderr, "[VSG]   %*s'%s' visible=%d selected=%d enabled=%d kind=%d children=%u\n",
							                           depth * 2,
							                           "",
							                           qPrintable(obj->getName()),
							                           obj->isVisible(),
							                           obj->isSelected(),
							                           obj->isEnabled(),
							                           static_cast<int>(obj->getClassID()),
							                           obj->getChildrenNumber());
							                   for (unsigned i = 0; i < obj->getChildrenNumber(); ++i)
							                   {
								                   dump(obj->getChild(i), depth + 1);
							                   }
						                   };
						                   fprintf(stderr, "[VSG] DB tree:\n");
						                   dump(dbRoot, 0);
					                   }
					                   if (auto* vsgView = dynamic_cast<ccVSGWindowInterface*>(view))
					                   {
						                   // force a scene sync (in case the incremental update
						                   // had not run yet) and re-check
						                   vsgView->redraw();
						                   fprintf(stderr, "[VSG] VSG scene root children: %zu\n",
						                           vsgView->sceneRoot()->children.size());
					                   }

					                   QImage image;
					                   if (auto* vsgView = dynamic_cast<ccVSGWindowInterface*>(view))
					                   {
						                   image = vsgView->renderToImage();
						                   fprintf(stderr, "[VSG] renderToImage: %dx%d (null=%d)\n",
						                           image.width(), image.height(), image.isNull());
					                   }
					                   else
					                   {
						                   fprintf(stderr, "[VSG] the active view is not a VSG one\n");
					                   }

					                   if (!image.isNull() && image.save(screenshotPath))
					                   {
						                   fprintf(stderr, "[VSG] screenshot saved: %s (%dx%d)\n",
						                           qPrintable(screenshotPath), image.width(), image.height());
					                   }
					                   else
					                   {
						                   fprintf(stderr, "[VSG] FAILED to save the screenshot: %s\n", qPrintable(screenshotPath));
					                   }
					                   fflush(stderr);

					                   QCoreApplication::quit();
				                   });
			}
		}
#endif

		// open the files the system asked to open during startup
		// (a FileOpen event, e.g. double-clicked in the macOS Finder)
		app.setMainWindowReady();

		// change the default path to the application one (do this AFTER processing the command line)
		QDir workingDir = QCoreApplication::applicationDirPath();

#ifdef Q_OS_MAC
		// This makes sure that our "working directory" is not within the application bundle
		if (workingDir.dirName() == "MacOS")
		{
			workingDir.cdUp();
			workingDir.cdUp();
			workingDir.cdUp();
		}
#endif

		QDir::setCurrent(workingDir.absolutePath());

		// let's rock!
		fprintf(stderr, "[VSG][trace] entering the event loop\n");
		fflush(stderr);
		try
		{
			result = QApplication::exec();
		}
		catch (const std::exception& e)
		{
			QMessageBox::warning(nullptr, "CC crashed!", QString("Hum, it seems that CC has crashed... Sorry about that :)\n") + e.what());
		}
		catch (...)
		{
			QMessageBox::warning(nullptr, "CC crashed!", "Hum, it seems that CC has crashed... Sorry about that :)");
		}

		// release the plugins
		for (ccPluginInterface* plugin : pluginManager.pluginList())
		{
			plugin->stop(); // just in case
		}
	}

	// release global structures
	MainWindow::DestroyInstance();
	FileIOFilter::UnregisterAll();

#ifdef CC_TRACK_ALIVE_SHARED_OBJECTS
	// for debug purposes
	unsigned alive = CCShareable::GetAliveCount();
	if (alive > 1)
	{
		printf("Error: some shared objects (%u) have not been released on program end!", alive);
		system("PAUSE");
	}
#endif

	return result;
}

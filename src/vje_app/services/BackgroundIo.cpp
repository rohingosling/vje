//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   BackgroundIo implementations. See the header for why the contract waits rather than calling back.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "services/BackgroundIo.hpp"

#include <QCoreApplication>
#include <QEventLoop>
#include <QMetaObject>
#include <QRunnable>
#include <QThreadPool>

namespace vje
{
	//=================================================================================================================
	// ThreadPoolIo
	//=================================================================================================================

	void ThreadPoolIo::run_and_wait ( const std::function<void ()>& work )
	{
		// Without a QCoreApplication there is no event loop to keep turning, so the wait would pump nothing and the
		// hand-off would buy only a context switch. This is the state a headless unit test constructs.

		if ( QCoreApplication::instance () == nullptr )
		{
			work ();

			return;
		}

		QEventLoop waitLoop;

		// Capturing by reference is safe precisely because this call waits: both objects outlive the runnable.
		//
		// Must be called from the thread owning the event loop -- the UI thread. Calling it FROM a pool thread could
		// deadlock against a saturated pool, which is why the only callers are the file commands.

		QThreadPool::globalInstance ()->start ( QRunnable::create ( [ &work, &waitLoop ] ()
		{
			work ();

			// Queued, so it is delivered inside exec() even when the work finished before the wait began -- a direct
			// quit() before exec() would be dropped and the caller would hang.

			QMetaObject::invokeMethod ( &waitLoop, "quit", Qt::QueuedConnection );
		} ) );

		// The window repaints while the work runs, but user input is held: a second File command started on top of this
		// one would reach a half-installed document.

		waitLoop.exec ( QEventLoop::ExcludeUserInputEvents );
	}

	//=================================================================================================================
	// ImmediateIo
	//=================================================================================================================

	void ImmediateIo::run_and_wait ( const std::function<void ()>& work )
	{
		work ();
	}
}

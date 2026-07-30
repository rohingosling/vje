//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   BackgroundIo -- where a load or a save actually runs (NFR-04: "file I/O and parsing run off the UI thread; the UI
//   never hard-freezes during load or save").
//
//   THE SHAPE, AND WHY IT IS THIS ONE. run_and_wait() hands the work to a worker thread and returns only once it has
//   finished, keeping the UI event loop turning while it waits. So the file lifecycle stays LINEAR -- open() parses,
//   then installs, then selects, in reading order -- while the parse itself is off the UI thread and the window keeps
//   painting.
//
//   The alternative, a callback-per-completion pipeline, was rejected for a specific reason rather than on taste: the
//   FILE-08 gate answers "Save" and must know the save SUCCEEDED before it lets the close proceed. Threading that
//   answer back through a continuation turns one linear decision into a state machine spanning three callbacks, and
//   every caller of the gate (Exit, window close, New, Open, Import) has to learn it. Documents are capped in the
//   10 MB region by NFR-03; what that costs is a brief unresponsive-to-INPUT window, not a frozen or ghost-white
//   window, and it is the freeze NFR-04 names that this removes.
//
//   STATED PLAINLY, so it is a decision and not an oversight: this is not a cancellable background load and there is no
//   progress dialog. Both are out of scope for version 2.0.
//
//   TWO IMPLEMENTATIONS:
//
//     ThreadPoolIo -- the application's. QThreadPool runs the work; the calling thread spins a QEventLoop with user
//                     input EXCLUDED until it completes, so the window repaints but no second File command can be
//                     started on top of the first (which is what would otherwise reach a half-installed document).
//     ImmediateIo  -- runs the work in place. The tests' form, and the honest fallback for any caller with no event
//                     loop of its own.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <functional>

namespace vje
{
	//*****************************************************************************************************************
	// Class: BackgroundIo
	//*****************************************************************************************************************

	class BackgroundIo
	{
		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		virtual ~BackgroundIo () = default;

		//=============================================================================================================
		// Public Interface
		//=============================================================================================================

	public:

		// Run work away from the calling thread and return once it has finished. Whatever work captures is safe to touch
		// on return -- the wait is a synchronization point, so no result needs a lock.

		virtual void run_and_wait ( const std::function<void ()>& work ) = 0;
	};

	//*****************************************************************************************************************
	// Class: ThreadPoolIo -- the application's implementation.
	//*****************************************************************************************************************

	class ThreadPoolIo : public BackgroundIo
	{
		//=============================================================================================================
		// Public Interface
		//=============================================================================================================

	public:

		void run_and_wait ( const std::function<void ()>& work ) override;
	};

	//*****************************************************************************************************************
	// Class: ImmediateIo -- runs the work in place (the tests' form).
	//*****************************************************************************************************************

	class ImmediateIo : public BackgroundIo
	{
		//=============================================================================================================
		// Public Interface
		//=============================================================================================================

	public:

		void run_and_wait ( const std::function<void ()>& work ) override;
	};
}

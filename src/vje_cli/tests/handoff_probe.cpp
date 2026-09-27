//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   A stand-in for vje.exe, used by tst_cli_process to observe the console twin's hand-off (CLI-07) without opening
//   a window. The test copies vje.com into an empty directory and this program beside it UNDER THE NAME vje.exe; the
//   twin, asked for the window, starts "its" window program -- which writes the arguments it received to the file
//   named by VJE_HANDOFF_RECORD and exits.
//
//   NO QT, deliberately: what is being observed is what Windows hands the child, so the probe reads the wide command
//   line itself, with nothing in between that could re-encode it. And a WINDOW program, like the vje.exe it stands in
//   for, so the hand-off is exercised with the same kind of child and no console flashes up during the test run.
//
//   The record is written to a side file and renamed into place, so the test never reads a half-written record.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <windows.h>
#include <shellapi.h>

#include <string>

int WINAPI WinMain ( HINSTANCE, HINSTANCE, LPSTR, int )
{
	wchar_t     recordPath [ 4096 ];
	const DWORD recordLength = GetEnvironmentVariableW ( L"VJE_HANDOFF_RECORD", recordPath, 4096 );

	if ( ( recordLength == 0 ) || ( recordLength >= 4096 ) )
	{
		return 1;
	}

	int     argumentCount = 0;
	LPWSTR* arguments     = CommandLineToArgvW ( GetCommandLineW (), &argumentCount );

	if ( arguments == nullptr )
	{
		return 1;
	}

	// One argument per line, the program name excluded, as UTF-8.

	std::wstring text;

	for ( int i = 1; i < argumentCount; ++i )
	{
		text += arguments [ i ];
		text += L'\n';
	}

	LocalFree ( arguments );

	const int   byteCount = WideCharToMultiByte ( CP_UTF8, 0, text.c_str (), static_cast<int> ( text.size () ), nullptr, 0, nullptr, nullptr );
	std::string bytes ( static_cast<std::size_t> ( byteCount ), '\0' );

	WideCharToMultiByte ( CP_UTF8, 0, text.c_str (), static_cast<int> ( text.size () ), bytes.data (), byteCount, nullptr, nullptr );

	const std::wstring partialPath = std::wstring ( recordPath ) + L".partial";

	const HANDLE file = CreateFileW ( partialPath.c_str (), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );

	if ( file == INVALID_HANDLE_VALUE )
	{
		return 1;
	}

	DWORD written = 0;

	WriteFile ( file, bytes.data (), static_cast<DWORD> ( bytes.size () ), &written, nullptr );
	CloseHandle ( file );

	return MoveFileExW ( partialPath.c_str (), recordPath, MOVEFILE_REPLACE_EXISTING ) ? 0 : 1;
}

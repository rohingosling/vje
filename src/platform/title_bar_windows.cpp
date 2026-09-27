//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   title_bar, Windows -- one DWM attribute, and the two user settings that take precedence over it (see the header).
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <platform/title_bar.hpp>

#include <windows.h>
#include <dwmapi.h>

namespace vje::platform
{
	namespace
	{
		// Named here rather than taken from dwmapi.h, whose MinGW copy does not carry every one of them. The values
		// are Windows' own and fixed.

		constexpr DWORD CAPTION_COLOR = 35;           // Windows 11 (build 22000) and later.
		constexpr DWORD COLOR_DEFAULT = 0xFFFFFFFF;   // CAPTION_COLOR's "the platform's own".

		bool registry_flag ( const wchar_t* key, const wchar_t* name )
		{
			DWORD value = 0;
			DWORD size  = sizeof ( value );

			const LSTATUS status = RegGetValueW ( HKEY_CURRENT_USER, key, name, RRF_RT_REG_DWORD, nullptr, &value, &size );

			return ( status == ERROR_SUCCESS ) && ( value != 0 );
		}
	}

	bool apply_title_bar_colour ( quintptr nativeWindow, std::optional<std::uint32_t> captionRgb )
	{
		// COLORREF is 0x00BBGGRR, the reverse of the 0xRRGGBB the caller speaks.

		const DWORD caption = captionRgb.has_value ()
		                    ? RGB ( ( *captionRgb >> 16 ) & 0xFF, ( *captionRgb >> 8 ) & 0xFF, *captionRgb & 0xFF )
		                    : COLOR_DEFAULT;

		return SUCCEEDED ( DwmSetWindowAttribute ( reinterpret_cast<HWND> ( nativeWindow ), CAPTION_COLOR, &caption, sizeof ( caption ) ) );
	}

	bool user_owns_title_bar_colour ()
	{
		// Windows' "Show accent colour on title bars and window borders" is ColorPrevalence under DWM -- not the value
		// of the same name under Themes\Personalize, which is the Start menu and taskbar's.

		if ( registry_flag ( L"Software\\Microsoft\\Windows\\DWM", L"ColorPrevalence" ) )
		{
			return true;
		}

		HIGHCONTRASTW contrast {};

		contrast.cbSize = sizeof ( contrast );

		return SystemParametersInfoW ( SPI_GETHIGHCONTRAST, sizeof ( contrast ), &contrast, 0 )
		    && ( ( contrast.dwFlags & HCF_HIGHCONTRASTON ) != 0 );
	}
}

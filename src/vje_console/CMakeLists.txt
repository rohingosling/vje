#-----------------------------------------------------------------------------------------------------------------------
# Project: VJE 2.0 -- vje_console (the Windows console twin, vje.com)
# Version: 2.0.0
# Date:    2024
# Author:  Rohin Gosling
#
# Description:
#
#   The console half of the Windows pair (CLI-07, architecture.md section 13.2). A Windows executable is linked as a
#   window program or a console program and neither serves both kinds of caller, so VJE ships two, built from the same
#   sources: vje.exe (vje_app, the window program) and this, vje.com. A terminal runs vje.com when `vje` is typed,
#   because PATHEXT lists .COM before .EXE -- Visual Studio's devenv.com / devenv.exe arrangement. It runs every command
#   itself, and hands a window launch to the vje.exe beside it.
#
#   ".com" is only a NAME. The file is an ordinary PE console executable; Windows chooses the loader from the file's
#   header, not its extension.
#
#   Windows only: on Linux the one executable, vje, serves the terminal and the desktop both.
#
#-----------------------------------------------------------------------------------------------------------------------

add_executable ( vje_console main.cpp )

target_link_libraries ( vje_console PRIVATE vje_cli vje_warnings )

# NO APPLICATION ICON, deliberately, unlike vje.exe (src/vje_app/CMakeLists.txt). The shell extracts no icon from a .com
# file whatever it contains -- measured 2026-09-26: the twin built with the resource reported no icon as vje.com and
# one icon when the same bytes were renamed .exe -- so embedding it here would change nothing anyone sees.

# Beside vje.exe, where the twin looks for it, under the same name the user types.

set_target_properties ( vje_console PROPERTIES
    OUTPUT_NAME              vje
    SUFFIX                   ".com"
    RUNTIME_OUTPUT_DIRECTORY "${VJE_EXECUTABLE_DIRECTORY}"
)

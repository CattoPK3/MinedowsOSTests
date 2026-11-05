#
# PROJECT:     LiberNT Boot Configuration Data editor
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Builds and packages the BCDEdit compatibility command
# COPYRIGHT:   Copyright 2026 Ahmed Arif
#

add_executable(bcdedit bcdedit.c ../bcdutil.c)
set_module_type(bcdedit win32cui UNICODE)
add_importlibs(bcdedit msvcrt advapi32 kernel32)
add_cd_file(TARGET bcdedit DESTINATION reactos/system32 FOR all)

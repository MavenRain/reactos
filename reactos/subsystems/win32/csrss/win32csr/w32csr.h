/* PSDK/NDK Headers */
#define WIN32_NO_STATUS
#include <windows.h>
#define NTOS_MODE_USER
#include <ndk/ntndk.h>

/* Our own BLUE.SYS Driver for Console Output */
#include <blue/ntddblue.h>

/* External Winlogon Header */
#include <winlogon.h>

/* Internal CSRSS Headers */
#include <api.h>
#include <conio.h>
#include <csrplugin.h>
#include <desktopbg.h>
#include "guiconsole.h"
#include "tuiconsole.h"
#include <win32csr.h>

#include <tchar.h>
#include <cpl.h>

#include "resource.h"

/* EOF */

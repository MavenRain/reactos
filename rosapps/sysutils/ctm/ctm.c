#define UNICODE
#define _UNICODE

/* Console Task Manager

   ctm.c - main program file

   Written by: Aleksey Bragin (aleksey@studiocerebral.com)
   
   Most of the code dealing with getting system parameters is taken from
   ReactOS Task Manager written by Brian Palmer (brianp@reactos.org)
   
   Localization features added by Herv� Poussineau (hpoussineau@fr.st)

   History:
   24 October 2004 - added localization features
	09 April 2003 - v0.1, fixed bugs, added features, ported to mingw
   	20 March 2003 - v0.03, works good under ReactOS, and allows process
			killing
	18 March 2003 - Initial version 0.01, doesn't work under RectOS
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */


//#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows //headers
#include <windows.h>
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>
#include <process.h>
#include <stdio.h>

#include <ddk/ntddk.h>
#include <epsapi.h>
#include <ntos/zwtypes.h>

#include "ctm.h"
#include "resource.h"

#define MAX_PROC 17
#define TIMES

HANDLE hStdin;
HANDLE hStdout;
HINSTANCE hInst;

DWORD inConMode;
DWORD outConMode;

DWORD columnRightPositions[5];
TCHAR lpSeparator[80];
TCHAR lpHeader[80];
TCHAR lpMemUnit[3];
TCHAR lpIdleProcess[80];;
TCHAR lpTitle[80];
TCHAR lpHeader[80];
TCHAR lpMenu[80];
TCHAR lpEmpty[80];

TCHAR KEY_QUIT, KEY_KILL;
TCHAR KEY_YES, KEY_NO;

const int		ProcPerScreen = 17; // 17 processess are displayed on one page
ULONG			ProcessCountOld = 0;
ULONG			ProcessCount = 0;

double			dbIdleTime;
double			dbKernelTime;
double			dbSystemTime;
LARGE_INTEGER		liOldIdleTime = {{0,0}};
double			OldKernelTime = 0;
LARGE_INTEGER		liOldSystemTime = {{0,0}};

PPERFDATA		pPerfDataOld = NULL;	// Older perf data (saved to establish delta values)
PPERFDATA		pPerfData = NULL;		// Most recent copy of perf data

int selection=0;
int scrolled=0;		// offset from which process start showing
int first = 0;		// first time in DisplayScreen

#define NEW_CONSOLE

// Functions that are needed by epsapi
void *PsaiMalloc(SIZE_T size) { return malloc(size); }
void *PsaiRealloc(void *ptr, SIZE_T size) { return realloc(ptr, size); }
void PsaiFree(void *ptr) { free(ptr); }

// Prototypes
unsigned int GetKeyPressed();

void GetInputOutputHandles()
{
#ifdef NEW_CONSOLE
	HANDLE console = CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE,
										FILE_SHARE_READ | FILE_SHARE_WRITE,
										0, CONSOLE_TEXTMODE_BUFFER, 0);

	if (SetConsoleActiveScreenBuffer(console) == FALSE)
	{
		hStdin = GetStdHandle(STD_INPUT_HANDLE);
		hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
	}
	else
	{
		hStdin = GetStdHandle(STD_INPUT_HANDLE);//console;
		hStdout = console;
	}
#else
	hStdin = GetStdHandle(STD_INPUT_HANDLE);
	hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
#endif
}

void RestoreConsole()
{
	SetConsoleMode(hStdin, inConMode);
	SetConsoleMode(hStdout, outConMode);

#ifdef NEW_CONSOLE
	SetConsoleActiveScreenBuffer(GetStdHandle(STD_OUTPUT_HANDLE));
#endif
}

void DisplayScreen()
{
	COORD pos;
	TCHAR lpStr[80];
	int posStr;
	DWORD numChars;
	int lines;
	int idx, i;

	if (first == 0)
	{
		// Header
		pos.X = 2; pos.Y = 2;
		WriteConsoleOutputCharacter(hStdout, lpTitle, _tcslen(lpTitle), pos, &numChars);

		pos.X = 2; pos.Y = 3;
		WriteConsoleOutputCharacter(hStdout, lpSeparator, _tcslen(lpSeparator), pos, &numChars);

		pos.X = 2; pos.Y = 4;
		WriteConsoleOutputCharacter(hStdout, lpHeader, _tcslen(lpHeader), pos, &numChars);

		pos.X = 2; pos.Y = 5;
		WriteConsoleOutputCharacter(hStdout, lpSeparator, _tcslen(lpSeparator), pos, &numChars);

		// Footer
		pos.X = 2; pos.Y = ProcPerScreen+6;
		WriteConsoleOutputCharacter(hStdout, lpSeparator, _tcslen(lpSeparator), pos, &numChars);
		
		// Menu
		pos.X = 2; pos.Y = ProcPerScreen+7;
		WriteConsoleOutputCharacter(hStdout, lpEmpty, _tcslen(lpEmpty), pos, &numChars);
		WriteConsoleOutputCharacter(hStdout, lpMenu, _tcslen(lpMenu), pos, &numChars);

		first = 1;
	}

    	// Processess
	lines = ProcessCount;
	if (lines > MAX_PROC)
		lines = MAX_PROC;
	for (idx=0; idx<MAX_PROC; idx++)
	{
		int len, i;
		TCHAR imgName[MAX_PATH];
		TCHAR lpPid[8];
		TCHAR lpCpu[6];
		TCHAR lpMemUsg[12];
		TCHAR lpPageFaults[15];
		WORD wColor;
		
		for (i = 0; i < 80; i++)
			lpStr[i] = _T(' ');

		// data
		if (idx < lines && scrolled + idx < ProcessCount)
		{
			// image name
#ifdef _UNICODE		
		   len = wcslen(pPerfData[scrolled+idx].ImageName);  
#else
		   WideCharToMultiByte(CP_ACP, 0, pPerfData[scrolled+idx].ImageName, -1,
			               imgName, MAX_PATH, NULL, NULL);
		   len = strlen(imgName);
#endif
			if (len > columnRightPositions[0])
			{
				len = columnRightPositions[0];
			}
#ifdef _UNICODE
		   wcsncpy(&lpStr[2], pPerfData[scrolled+idx].ImageName, len);
#else
		   strncpy(&lpStr[2], imgName, len);
#endif

			// PID
		   _stprintf(lpPid, _T("%6ld"), pPerfData[scrolled+idx].ProcessId);
		 	_tcsncpy(&lpStr[columnRightPositions[1] - 6], lpPid, 6);

#ifdef TIMES
			// CPU
			_stprintf(lpCpu, _T("%3d%%"), pPerfData[scrolled+idx].CPUUsage);
			_tcsncpy(&lpStr[columnRightPositions[2] - 4], lpCpu, 4);
#endif

			// Mem usage
			 _stprintf(lpMemUsg, _T("%6ld %s"), pPerfData[scrolled+idx].WorkingSetSizeBytes / 1024, lpMemUnit);
			 _tcsncpy(&lpStr[columnRightPositions[3] - 9], lpMemUsg, 9);

			// Page Fault
			_stprintf(lpPageFaults, _T("%12ld"), pPerfData[scrolled+idx].PageFaultCount);
			_tcsncpy(&lpStr[columnRightPositions[4] - 12], lpPageFaults, 12);
		}

		// columns
		lpStr[0] = _T(' ');
		lpStr[1] = _T('|');
		for (i = 0; i < 5; i++)
			lpStr[columnRightPositions[i] + 1] = _T('|');
                pos.X = 1; pos.Y = 6+idx;
		WriteConsoleOutputCharacter(hStdout, lpStr, 74, pos, &numChars);

		// Attributes now...
		pos.X = 3; pos.Y = 6+idx;
		if (selection == idx)
		{
			wColor = BACKGROUND_GREEN | 
				FOREGROUND_RED | 
				FOREGROUND_GREEN | 
				FOREGROUND_BLUE;
		}
		else
		{
			wColor = BACKGROUND_BLUE |
					FOREGROUND_RED | 
					FOREGROUND_GREEN | 
					FOREGROUND_BLUE;
		}

		FillConsoleOutputAttribute( 
			hStdout,          // screen buffer handle 
			wColor,           // color to fill with 
			columnRightPositions[0] - 1,	// number of cells to fill
			pos,            // first cell to write to 
			&numChars);       // actual number written 
	}

	return;
}

// returns TRUE if exiting
int ProcessKeys(int numEvents)
{
	DWORD numChars;
	if ((ProcessCount-scrolled < 17) && (ProcessCount > 17))
		scrolled = ProcessCount-17;

	TCHAR key = GetKeyPressed(numEvents);
	if (key == KEY_QUIT)
		return TRUE;
	else if (key == KEY_KILL)
	{
		// user wants to kill some process, get his acknowledgement
		DWORD pId;
		COORD pos;
		TCHAR lpStr[100];

		pos.X = 2; pos.Y = 24;
		if (LoadString(hInst, IDS_KILL_PROCESS, lpStr, 100))
			WriteConsoleOutputCharacter(hStdout, lpStr, _tcslen(lpStr), pos, &numChars);

		do {
			GetNumberOfConsoleInputEvents(hStdin, &pId);
			key = GetKeyPressed(pId);
		} while (key == 0);

		if (key == KEY_YES)
		{
			HANDLE hProcess;
			pId = pPerfData[selection+scrolled].ProcessId;
			hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pId);
			
			if (hProcess)
			{
				if (!TerminateProcess(hProcess, 0))
				{
					if (LoadString(hInst, IDS_KILL_PROCESS_ERR1, lpStr, 80))
					{
						WriteConsoleOutputCharacter(hStdout, lpEmpty, _tcslen(lpEmpty), pos, &numChars);
						WriteConsoleOutputCharacter(hStdout, lpStr, _tcslen(lpStr), pos, &numChars);
					}
					Sleep(1000);
				}

				CloseHandle(hProcess);
			}
			else
			{
				if (LoadString(hInst, IDS_KILL_PROCESS_ERR2, lpStr, 80))
				{
					WriteConsoleOutputCharacter(hStdout, lpEmpty, _tcslen(lpEmpty), pos, &numChars);
					_stprintf(lpStr, lpStr, pId);
					WriteConsoleOutputCharacter(hStdout, lpStr, _tcslen(lpStr), pos, &numChars);
				}
				Sleep(1000);
			}
		}
		
		first = 0;
	}
	else if (key == VK_UP)
	{
		if (selection > 0)
			selection--;
		else if ((selection == 0) && (scrolled > 0))
			scrolled--;
	}
	else if (key == VK_DOWN)
	{
		if ((selection < MAX_PROC-1) && (selection < ProcessCount-1))
			selection++;
		else if ((selection == MAX_PROC-1) && (selection+scrolled < ProcessCount-1))
			scrolled++;
	}
	
	return FALSE;
}

void PerfDataRefresh()
{
	LONG							status;
	ULONG							ulSize;
	LPBYTE							pBuffer;
	ULONG							BufferSize;
	ULONG							Idx, Idx2;
	HANDLE							hProcess;
	HANDLE							hProcessToken;
	PSYSTEM_PROCESSES		pSPI;
	PPERFDATA						pPDOld;
	TCHAR							szTemp[MAX_PATH];
	DWORD							dwSize;
	double							CurrentKernelTime;
	PSYSTEM_PROCESSORTIME_INFO		SysProcessorTimeInfo;
	SYSTEM_PERFORMANCE_INFORMATION	SysPerfInfo;
	SYSTEM_TIMEOFDAY_INFORMATION            SysTimeInfo;

#ifdef TIMES
	// Get new system time
	status = NtQuerySystemInformation(SystemTimeInformation, &SysTimeInfo, sizeof(SysTimeInfo), 0);
	if (status != NO_ERROR)
		return;

	// Get new CPU's idle time
	status = NtQuerySystemInformation(SystemPerformanceInformation, &SysPerfInfo, sizeof(SysPerfInfo), NULL);
	if (status != NO_ERROR)
		return;
#endif
	// Get processor information	
	SysProcessorTimeInfo = (PSYSTEM_PROCESSORTIME_INFO)malloc(sizeof(SYSTEM_PROCESSORTIME_INFO) * 1/*SystemBasicInfo.bKeNumberProcessors*/);
	status = NtQuerySystemInformation(SystemProcessorTimes, SysProcessorTimeInfo, sizeof(SYSTEM_PROCESSORTIME_INFO) * 1/*SystemBasicInfo.bKeNumberProcessors*/, &ulSize);


	// Get process information
	PsaCaptureProcessesAndThreads((PSYSTEM_PROCESSES *)&pBuffer);

#ifdef TIMES
	for (CurrentKernelTime=0, Idx=0; Idx<1/*SystemBasicInfo.bKeNumberProcessors*/; Idx++) {
		CurrentKernelTime += Li2Double(SysProcessorTimeInfo[Idx].TotalProcessorTime);
		CurrentKernelTime += Li2Double(SysProcessorTimeInfo[Idx].TotalDPCTime);
		CurrentKernelTime += Li2Double(SysProcessorTimeInfo[Idx].TotalInterruptTime);
	}

	// If it's a first call - skip idle time calcs
	if (liOldIdleTime.QuadPart != 0) {
		// CurrentValue = NewValue - OldValue
		dbIdleTime = Li2Double(SysPerfInfo.IdleTime) - Li2Double(liOldIdleTime);
		dbKernelTime = CurrentKernelTime - OldKernelTime;
		dbSystemTime = Li2Double(SysTimeInfo.CurrentTime) - Li2Double(liOldSystemTime);

		// CurrentCpuIdle = IdleTime / SystemTime
		dbIdleTime = dbIdleTime / dbSystemTime;
		dbKernelTime = dbKernelTime / dbSystemTime;
		
		// CurrentCpuUsage% = 100 - (CurrentCpuIdle * 100) / NumberOfProcessors
		dbIdleTime = 100.0 - dbIdleTime * 100.0; /* / (double)SystemBasicInfo.bKeNumberProcessors;// + 0.5; */
		dbKernelTime = 100.0 - dbKernelTime * 100.0; /* / (double)SystemBasicInfo.bKeNumberProcessors;// + 0.5; */
	}

	// Store new CPU's idle and system time
	liOldIdleTime = SysPerfInfo.IdleTime;
	liOldSystemTime = SysTimeInfo.CurrentTime;
	OldKernelTime = CurrentKernelTime;
#endif

	// Determine the process count
	// We loop through the data we got from PsaCaptureProcessesAndThreads
	// and count how many structures there are (until PsaWalkNextProcess
        // returns NULL)
	ProcessCountOld = ProcessCount;
	ProcessCount = 0;
        pSPI = PsaWalkFirstProcess((PSYSTEM_PROCESSES)pBuffer);
	while (pSPI) {
		ProcessCount++;
		pSPI = PsaWalkNextProcess(pSPI);
	}

	// Now alloc a new PERFDATA array and fill in the data
	if (pPerfDataOld) {
		free(pPerfDataOld);
	}
	pPerfDataOld = pPerfData;
	pPerfData = (PPERFDATA)malloc(sizeof(PERFDATA) * ProcessCount);
        pSPI = PsaWalkFirstProcess((PSYSTEM_PROCESSES)pBuffer);
	for (Idx=0; Idx<ProcessCount; Idx++) {
		// Get the old perf data for this process (if any)
		// so that we can establish delta values
		pPDOld = NULL;
		for (Idx2=0; Idx2<ProcessCountOld; Idx2++) {
			if (pPerfDataOld[Idx2].ProcessId == pSPI->ProcessId) {
				pPDOld = &pPerfDataOld[Idx2];
				break;
			}
		}

		// Clear out process perf data structure
		memset(&pPerfData[Idx], 0, sizeof(PERFDATA));

		if (pSPI->ProcessName.Buffer) {
			wcsncpy(pPerfData[Idx].ImageName, pSPI->ProcessName.Buffer, pSPI->ProcessName.Length / sizeof(WCHAR));
                        pPerfData[Idx].ImageName[pSPI->ProcessName.Length / sizeof(WCHAR)] = 0;
		}
		else
		{
#ifdef _UNICODE
			wcscpy(pPerfData[Idx].ImageName, lpIdleProcess);
#else
			MultiByteToWideChar(CP_ACP, 0, lpIdleProcess, strlen(lpIdleProcess), pPerfData[Idx].ImageName, MAX_PATH);
#endif
		}

		pPerfData[Idx].ProcessId = pSPI->ProcessId;

		if (pPDOld)	{
#ifdef TIMES
			double	CurTime = Li2Double(pSPI->KernelTime) + Li2Double(pSPI->UserTime);
			double	OldTime = Li2Double(pPDOld->KernelTime) + Li2Double(pPDOld->UserTime);
			double	CpuTime = (CurTime - OldTime) / dbSystemTime;
			CpuTime = CpuTime * 100.0; /* / (double)SystemBasicInfo.bKeNumberProcessors;// + 0.5;*/

			pPerfData[Idx].CPUUsage = (ULONG)CpuTime;
#else
			pPerfData[Idx].CPUUsage = 0;
#endif
		}

		pPerfData[Idx].CPUTime.QuadPart = pSPI->UserTime.QuadPart + pSPI->KernelTime.QuadPart;
		pPerfData[Idx].WorkingSetSizeBytes = pSPI->VmCounters.WorkingSetSize;
		pPerfData[Idx].PeakWorkingSetSizeBytes = pSPI->VmCounters.PeakWorkingSetSize;
		if (pPDOld)
			pPerfData[Idx].WorkingSetSizeDelta = labs((LONG)pSPI->VmCounters.WorkingSetSize - (LONG)pPDOld->WorkingSetSizeBytes);
		else
			pPerfData[Idx].WorkingSetSizeDelta = 0;
		pPerfData[Idx].PageFaultCount = pSPI->VmCounters.PageFaultCount;
		if (pPDOld)
			pPerfData[Idx].PageFaultCountDelta = labs((LONG)pSPI->VmCounters.PageFaultCount - (LONG)pPDOld->PageFaultCount);
		else
			pPerfData[Idx].PageFaultCountDelta = 0;
		pPerfData[Idx].VirtualMemorySizeBytes = pSPI->VmCounters.VirtualSize;
		pPerfData[Idx].PagedPoolUsagePages = pSPI->VmCounters.QuotaPagedPoolUsage;
		pPerfData[Idx].NonPagedPoolUsagePages = pSPI->VmCounters.QuotaNonPagedPoolUsage;
		pPerfData[Idx].BasePriority = pSPI->BasePriority;
		pPerfData[Idx].HandleCount = pSPI->HandleCount;
		pPerfData[Idx].ThreadCount = pSPI->ThreadCount;
		//pPerfData[Idx].SessionId = pSPI->SessionId;

#ifdef EXTRA_INFO
		hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pSPI->ProcessId);
		if (hProcess) {
			if (OpenProcessToken(hProcess, TOKEN_QUERY|TOKEN_DUPLICATE|TOKEN_IMPERSONATE, &hProcessToken)) {
				ImpersonateLoggedOnUser(hProcessToken);
				memset(szTemp, 0, sizeof(TCHAR[MAX_PATH]));
				dwSize = MAX_PATH;
				GetUserName(szTemp, &dwSize);
#ifndef UNICODE
				MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, szTemp, -1, pPerfData[Idx].UserName, MAX_PATH);
/*
int MultiByteToWideChar(
  UINT CodePage,         // code page
  DWORD dwFlags,         // character-type options
  LPCSTR lpMultiByteStr, // string to map
  int cbMultiByte,       // number of bytes in string
  LPWSTR lpWideCharStr,  // wide-character buffer
  int cchWideChar        // size of buffer
);
 */
#endif
				RevertToSelf();
				CloseHandle(hProcessToken);
			}
			CloseHandle(hProcess);
		}
#endif
#ifdef TIMES
		pPerfData[Idx].UserTime.QuadPart = pSPI->UserTime.QuadPart;
		pPerfData[Idx].KernelTime.QuadPart = pSPI->KernelTime.QuadPart;
#endif
		pSPI = PsaWalkNextProcess(pSPI);
	}
	PsaFreeCapture(pBuffer);

	free(SysProcessorTimeInfo);
}

// Code partly taken from slw32tty.c from mc/slang
unsigned int GetKeyPressed(int events)
{
	long key;
	DWORD bytesRead;
	INPUT_RECORD record;
	int i;


	for (i=0; i<events; i++)
	{
		if (!ReadConsoleInput(hStdin, &record, 0, &bytesRead)) {
			return 0;
		}
		if (!ReadConsoleInput(hStdin, &record, 1, &bytesRead)) {
			return 0;
		}

		if (record.EventType == KEY_EVENT && record.Event.KeyEvent.bKeyDown)
			return record.Event.KeyEvent.wVirtualKeyCode;//.uChar.AsciiChar;
	}

	return 0;
}


int main(int *argc, char **argv)
{
	int i;
	TCHAR lpStr[80];
	
	for (i = 0; i < 80; i++)
		lpEmpty[i] = lpHeader[i] = _T(' ');
	lpEmpty[79] = _T('\0');

	/* Initialize global variables */
	hInst = 0 /* FIXME: which value? [used with LoadString(hInst, ..., ..., ...)] */;
	if (LoadString(hInst, IDS_COLUMN_IMAGENAME, lpStr, 80))
	{
		columnRightPositions[0] = _tcslen(lpStr);
		_tcsncpy(&lpHeader[2], lpStr, _tcslen(lpStr));
	}
	if (LoadString(hInst, IDS_COLUMN_PID, lpStr, 80))
	{
		columnRightPositions[1] = columnRightPositions[0] + _tcslen(lpStr) + 3;
		_tcsncpy(&lpHeader[columnRightPositions[0] + 2], lpStr, _tcslen(lpStr));
	}
	if (LoadString(hInst, IDS_COLUMN_CPU, lpStr, 80))
	{
		columnRightPositions[2] = columnRightPositions[1] + _tcslen(lpStr) + 3;
		_tcsncpy(&lpHeader[columnRightPositions[1] + 2], lpStr, _tcslen(lpStr));
	}
	if (LoadString(hInst, IDS_COLUMN_MEM, lpStr, 80))
	{
		columnRightPositions[3] = columnRightPositions[2] + _tcslen(lpStr) + 3;
		_tcsncpy(&lpHeader[columnRightPositions[2] + 2], lpStr, _tcslen(lpStr));
	}
	if (LoadString(hInst, IDS_COLUMN_PF, lpStr, 80))
	{
		columnRightPositions[4] = columnRightPositions[3] + _tcslen(lpStr) + 3;
		_tcsncpy(&lpHeader[columnRightPositions[3] + 2], lpStr, _tcslen(lpStr));
	}
	
	for (i = 0; i < columnRightPositions[4]; i++)
		lpSeparator[i] = _T('-');
	lpHeader[0] = _T('|');
	lpSeparator[0] = _T('+');
	for (i = 0; i < 5; i++)
	{
		lpHeader[columnRightPositions[i]] = _T('|');
		lpSeparator[columnRightPositions[i]] = _T('+');
	}
	lpSeparator[columnRightPositions[4] + 1] = _T('\0');
	
	if (!LoadString(hInst, IDS_APP_TITLE, lpTitle, 80))
		lpTitle[0] = _T('\0');
	if (!LoadString(hInst, IDS_COLUMN_MEM_UNIT, lpMemUnit, 3))
		lpMemUnit[0] = _T('\0');
	if (!LoadString(hInst, IDS_MENU, lpMenu, 80))
		lpMenu[0] = _T('\0');
	if (!LoadString(hInst, IDS_IDLE_PROCESS, lpIdleProcess, 80))
		lpIdleProcess[0] = _T('\0');
	
	if (LoadString(hInst, IDS_MENU_QUIT, lpStr, 2))
		KEY_QUIT = lpStr[0];
	if (LoadString(hInst, IDS_MENU_KILL_PROCESS, lpStr, 2))
		KEY_KILL = lpStr[0];
	if (LoadString(hInst, IDS_YES, lpStr, 2))
		KEY_YES = lpStr[0];
	if (LoadString(hInst, IDS_NO, lpStr, 2))
		KEY_NO = lpStr[0];

	GetInputOutputHandles();

	if (hStdin == INVALID_HANDLE_VALUE || hStdout == INVALID_HANDLE_VALUE)
	{
		if (LoadString(hInst, IDS_CTM_GENERAL_ERR1, lpStr, 80))
			_tprintf(lpStr);
		return -1;
	}

	if (GetConsoleMode(hStdin, &inConMode) == 0)
	{
		if (LoadString(hInst, IDS_CTM_GENERAL_ERR2, lpStr, 80))
			_tprintf(lpStr);
		return -1;
	}

	if (GetConsoleMode(hStdout, &outConMode) == 0)
	{
		if (LoadString(hInst, IDS_CTM_GENERAL_ERR3, lpStr, 80))
			_tprintf(lpStr);
		return -1;
	}

	SetConsoleMode(hStdin, 0); //FIXME: Should check for error!
	SetConsoleMode(hStdout, 0); //FIXME: Should check for error!

	while (1)
	{
		DWORD numEvents;

		PerfDataRefresh();
		DisplayScreen();

		//WriteConsole(hStdin, " ", 1, &numEvents, NULL); // TODO: Make another way (this is ugly, I know)
#if 0
		/* WaitForSingleObject for console handles is not implemented in ROS */
		WaitForSingleObject(hStdin, 1000);
#endif

		GetNumberOfConsoleInputEvents(hStdin, &numEvents);

		if (numEvents > 0)
		{
			if (ProcessKeys(numEvents) == TRUE)
				break;
		}
#if 1
		else
		{
		    /* Should be removed, if WaitForSingleObject is implemented for console handles */
		    Sleep(40); // TODO: Should be done more efficient (might be another thread handling input/etc)*/
		}
#endif
	}

	RestoreConsole();
	return 0;
}

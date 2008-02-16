#ifndef MUI_H__
#define MUI_H__

typedef struct
{
   BYTE X;
   BYTE Y;
   LPCSTR Buffer;
   BYTE Flags;
}MUI_ENTRY, *PMUI_ENTRY;

typedef struct
{
    LPCSTR ErrorText;
    LPCSTR ErrorStatus;
}MUI_ERROR;

typedef struct
{
    LONG Number;
    MUI_ENTRY * MuiEntry;
}MUI_PAGE;

typedef struct
{
    LONG Number;
    LPSTR String;
} MUI_STRING;

typedef struct
{
    PWCHAR LanguageID;
    PWCHAR LanguageKeyboardLayoutID;
    PWCHAR ACPage;
    PWCHAR OEMCPage;
    PWCHAR MACCPage;
    PWCHAR LanguageDescriptor;
    const MUI_PAGE * MuiPages;
    const MUI_ERROR * MuiErrors;
    const MUI_STRING * MuiStrings;
}MUI_LANGUAGE;


#define TEXT_NORMAL            0
#define TEXT_HIGHLIGHT         1
#define TEXT_UNDERLINE         2
#define TEXT_STATUS            4

#define TEXT_ALIGN_DEFAULT     8
#define TEXT_ALIGN_RIGHT       16
#define TEXT_ALIGN_LEFT        32
#define TEXT_ALIGN_CENTER      64

VOID
MUIDisplayPage (ULONG PageNumber);

VOID
MUIDisplayError (ULONG ErrorNum, PINPUT_RECORD Ir, ULONG WaitEvent);

LPCWSTR
MUIDefaultKeyboardLayout(VOID);

BOOLEAN
AddCodePage(VOID);

VOID
SetConsoleCodePage(VOID);

LPSTR
MUIGetString(ULONG Number);

#define STRING_PLEASEWAIT 1
#define STRING_INSTALLCREATEPARTITION 2
#define STRING_INSTALLDELETEPARTITION 3
#define STRING_PARTITIONSIZE 4
#define STRING_CHOOSENEWPARTITION 5
#define STRING_HDDSIZE 6
#define STRING_CREATEPARTITION 7
#define STRING_PARTFORMAT 8
#define STRING_NONFORMATTEDPART 9
#define STRING_INSTALLONPART 10
#define STRING_CHECKINGPART 11
#define STRING_QUITCONTINUE 12
#define STRING_REBOOTCOMPUTER 13
#define STRING_TXTSETUPFAILED 14
#define STRING_COPYING 15
#define STRING_SETUPCOPYINGFILES 16
#define STRING_PAGEDMEM 17
#define STRING_NONPAGEDMEM 18
#define STRING_FREEMEM 19
#define STRING_REGHIVEUPDATE 20
#define STRING_IMPORTFILE 21
#define STRING_DISPLAYETTINGSUPDATE 22
#define STRING_LOCALESETTINGSUPDATE 23
#define STRING_KEYBOARDSETTINGSUPDATE 24
#define STRING_CODEPAGEINFOUPDATE 25
#define STRING_DONE 26
#define STRING_REBOOTCOMPUTER2 27
#define STRING_CONSOLEFAIL1 28
#define STRING_CONSOLEFAIL2 29
#define STRING_CONSOLEFAIL3 30
#define STRING_FORMATTINGDISK 31
#define STRING_CHECKINGDISK 32
#define STRING_FORMATDISK1 33
#define STRING_FORMATDISK2 34
#define STRING_KEEPFORMAT 35
#define STRING_HDDINFO1 36
#define STRING_HDDINFO2 37
#define STRING_HDDINFO3 38
#define STRING_HDDINFO4 39
#define STRING_HDDINFO5 40
#define STRING_HDDINFO6 41
#define STRING_HDDINFO7 42
#define STRING_HDDINFO8 43
#define STRING_HDDINFO9 44
#define STRING_HDDINFO10 45
#define STRING_HDDINFO11 46
#define STRING_NEWPARTITION 47
#define STRING_UNPSPACE 48
#endif

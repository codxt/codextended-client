#include <windows.h>
#include <ctime>
#include "hooking.h"
#include "cl.h"
#include "shared.h"
#pragma comment(lib, "vendor/detours/detours.lib")
#include "../vendor/detours/detours.h"

#ifdef DEBUG
extern HANDLE hLogFile = INVALID_HANDLE_VALUE;
#endif
int codversion;
typedef enum
{
	COD1_1_1_MP,
	COD1_1_1_SP,
	COD1_1_5_MP,
	COD1_1_5_SP
} cod_v;

void Main_UnprotectModule(HMODULE hModule)
{
	PIMAGE_DOS_HEADER header = (PIMAGE_DOS_HEADER)hModule;
	PIMAGE_NT_HEADERS ntHeader = (PIMAGE_NT_HEADERS)((DWORD)hModule + header->e_lfanew);

	SIZE_T size = ntHeader->OptionalHeader.SizeOfImage;
	DWORD oldProtect;
	VirtualProtect((LPVOID)hModule, size, PAGE_EXECUTE_READWRITE, &oldProtect);
}

HMODULE(WINAPI* orig_LoadLibraryA)(LPCSTR lpFileName);
HMODULE WINAPI hLoadLibraryA(LPSTR lpFileName)
{
	HMODULE hModule = orig_LoadLibraryA(lpFileName);
	DWORD pBase = (DWORD)GetModuleHandleA(lpFileName);

	if (!pBase)
		return hModule;
	
	if (strstr(lpFileName, "ui_mp"))
	{
		if (codversion != COD1_1_1_MP && codversion != COD1_1_5_MP)
			return hModule;

		void UI_Init(DWORD);
		UI_Init(pBase);
	}
	else if (strstr(lpFileName, "cgame_mp"))
	{
		if (codversion != COD1_1_1_MP && codversion != COD1_1_5_MP)
			return hModule;

		void CG_Init(DWORD);
		CG_Init(pBase);
	}
	else if (strstr(lpFileName, "game_mp"))
	{
		void G_Init(DWORD);
		G_Init(pBase);
	}

#ifdef DEBUG
	Com_Printf("hLoadLibraryA: ^2lpFileName = %s\n", lpFileName);
#endif
	return hModule;
}

extern DWORD cgame_mp;
BOOL(WINAPI* orig_FreeLibrary)(HMODULE hModule);
BOOL WINAPI hFreeLibrary(HMODULE hModule)
{
	CHAR szFileName[MAX_PATH];
	if (GetModuleFileNameA(hModule, szFileName, sizeof(szFileName)))
	{
		if (strstr(szFileName, "cgame_mp"))
			cgame_mp = 0; //TODO: search for some "cgameInitialized"/"cgameStarted" variable to use instead of hooking FreeLibrary just for this.
#ifdef DEBUG
		Com_Printf("hFreeLibrary: ^szFileName = %s\n", szFileName);
#endif
	}
	return orig_FreeLibrary(hModule);
}

void patch_opcode_loadlibrary(void)
{
	orig_LoadLibraryA = (struct HINSTANCE__* (__stdcall*)(const char*)) \
		DetourFunction((LPBYTE)LoadLibraryA, (LPBYTE)hLoadLibraryA);
}
void patch_opcode_freelibrary(void)
{
	orig_FreeLibrary = (BOOL(WINAPI*)(HMODULE)) \
		DetourFunction((LPBYTE)FreeLibrary, (LPBYTE)hFreeLibrary);
}

static bool is_addr_safe(size_t addr)
{
	__try
	{
		*(unsigned char*)(addr);
	}
	__except (1)
	{
		return false;
	}
	return true;
}
bool verifyCodVersion()
{
#ifdef PATCH_1_1
	int addressMP = 0x566C18;
	int addressSP = 0x555494;
	const char* versionMP = "1.1";
	const char* versionSP = "1.0";
#elif PATCH_1_5
	int addressMP = 0x005a60d0;
	int addressSP = 0x005565ac;
	char* versionMP = "1.5";
	char* versionSP = "1.3";
#endif

	if (is_addr_safe(addressMP))
	{
		char* patchVersion = (char*)addressMP;
		if (patchVersion && !strcmp(patchVersion, versionMP))
		{
#ifdef PATCH_1_1
			codversion = COD1_1_1_MP;
#elif PATCH_1_5
			codversion = COD1_1_5_MP;
#endif
			return true;
		}
	}

	if (is_addr_safe(addressSP))
	{
		char* patchVersion = (char*)addressSP;
		if (patchVersion && !strcmp(patchVersion, versionSP))
		{
#ifdef PATCH_1_1
			codversion = COD1_1_1_SP;
#elif PATCH_1_5
			codversion = COD1_1_5_SP;
#endif
			return true;
		}
	}

	return false;
}

void cleanupExit()
{
	void(*o)();
#ifdef PATCH_1_1
	* (UINT32*)&o = 0x40E2B0;
#elif PATCH_1_5
	* (UINT32*)&o = 0x0040ef70;
#endif
	o();

	void Sys_Unload();
	Sys_Unload();
}

#ifdef PATCH_1_1
static void(*Com_Quit_f)() = (void(*)())0x435D80;
#elif PATCH_1_5
static void(*Com_Quit_f)() = (void(*)())0x00438220;
#endif

bool applyHooks()
{
#ifdef PATCH_1_1
	unlock_client_structure(); // make some client cls structure members writeable
#endif

#ifdef PATCH_1_1
	/*by lstolcman*/
	// allow alt tab - set dwExStyle from WS_EX_TOPMOST to WS_EX_LEFT (default), which allows minimizing
	XUNLOCK((void*)0x5083b1, 1);
	memset((void*)0x5083b1, 0x00, 1);
	/**/
#endif

	patch_opcode_loadlibrary();
	patch_opcode_freelibrary();
	void patch_opcode_glbindtexture(void);
	patch_opcode_glbindtexture();

	int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow);
#ifdef PATCH_1_1
	__call(0x528948, (int)WinMain);
#elif PATCH_1_5
	__call(0x00560f99, (int)WinMain);
#endif

	LRESULT CALLBACK h_WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
#ifdef PATCH_1_1
	* (int*)(0x4639b9 + 1) = (int)h_WndProc;
#elif PATCH_1_5
	* (int*)(0x00468db9 + 1) = (int)h_WndProc;
#endif

	void _CL_Init();
#ifdef PATCH_1_1
	__call(0x437B4B, (int)_CL_Init);
	__call(0x438178, (int)_CL_Init);
#elif PATCH_1_5
	__call(0x00439fca, (int)_CL_Init);
	__call(0x0043a617, (int)_CL_Init);
#endif

#ifdef PATCH_1_5
	void _CL_InitCGame(void);
	__call(0x004109c4, (int)_CL_InitCGame);
	__call(0x00410d2b, (int)_CL_InitCGame);
#endif

#ifdef PATCH_1_5
	void _CL_SystemInfoChanged(void);
	__call(0x004015fc, (int)_CL_SystemInfoChanged);
	__call(0x00417a78, (int)_CL_SystemInfoChanged);
#endif

#ifdef PATCH_1_1
	void CL_Frame(int msec);
	__call(0x43822C, (int)CL_Frame);

	char* __cdecl CL_SetServerInfo_HostnameStrncpy(char*, char*, size_t);
	__call(0x412A2C, (int)CL_SetServerInfo_HostnameStrncpy);

	void _CL_Connect_f();
	XUNLOCK((void*)0x41269B, 5);
	*(UINT32*)(0x41269B + 1) = (int)_CL_Connect_f;

	void _CL_NextDownload();
	__call(0x410316, (int)_CL_NextDownload);
	__call(0x410376, (int)_CL_NextDownload);
	__call(0x41656C, (int)_CL_NextDownload);

	void Field_CharEvent_IgnoreTilde();
	__jmp(0x40CB1E, (int)Field_CharEvent_IgnoreTilde);
#endif

#ifdef PATCH_1_1
	__call(0x46319B, (int)cleanupExit);
#elif PATCH_1_5
	__call(0x004684c5, (int)cleanupExit);
#endif
	return true;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
	{
		HMODULE hModule = GetModuleHandle(NULL); // codmp.exe
		if (hModule)
			Main_UnprotectModule(hModule);

#if 0
#ifdef DEBUG
		if (hLogFile == INVALID_HANDLE_VALUE)
		{
			hLogFile = CreateFile(L"./memlog.txt",
				GENERIC_WRITE,
				FILE_SHARE_WRITE, NULL, CREATE_ALWAYS,
				FILE_ATTRIBUTE_NORMAL, NULL);
			_CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
			_CrtSetReportFile(_CRT_WARN, hLogFile);
		}
#endif
#endif

		if (!verifyCodVersion())
		{
			MessageBoxA(NULL, "CoD version verification failed", "c1cx", MB_OK | MB_ICONERROR);
			return FALSE;
		}
		else if ((codversion == COD1_1_1_MP || codversion == COD1_1_5_MP) && !applyHooks())
		{
			MessageBoxA(NULL, "Hooking failed", "c1cx", MB_OK | MB_ICONERROR);
			Com_Quit_f();
		}
	}
	break;

	case DLL_PROCESS_DETACH:
	{

	}
	break;

	}
	return TRUE;
}
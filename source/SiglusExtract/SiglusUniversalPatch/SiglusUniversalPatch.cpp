#include "my.h"
#include <string>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <locale>
#include <cctype>
#include <codecvt>
#include "json/json.h"

#pragma comment(lib, "Version.lib")

ForceInline std::wstring FASTCALL ReplaceFileNameExtension(std::wstring& Path, PCWSTR NewExtensionName)
{
	ULONG_PTR Ptr;

	Ptr = Path.find_last_of(L".");
	if (Ptr == std::wstring::npos)
		return Path + NewExtensionName;

	return Path.substr(0, Ptr) + NewExtensionName;
}


ForceInline std::wstring FASTCALL GetFileName(std::wstring& Path)
{
	ULONG_PTR Ptr;

	Ptr = Path.find_last_of(L"\\");
	if (Ptr == std::wstring::npos)
		return Path;

	return Path.substr(Ptr + 1, std::wstring::npos);
}


ForceInline std::tuple<std::wstring, std::wstring> FASTCALL GetFileNameAndBaseDir(std::wstring& Path)
{
	ULONG_PTR Ptr;

	Ptr = Path.find_last_of(L"\\");
	if (Ptr == std::wstring::npos)
		return { std::wstring(), Path };

	return { Path.substr(0, Ptr + 1), Path.substr(Ptr + 1, std::wstring::npos) };
}


ForceInline std::wstring FASTCALL GetFileNameExtension(std::wstring& Path)
{
	ULONG_PTR Ptr;

	Ptr = Path.find_last_of(L".");
	if (Ptr == std::wstring::npos)
		return NULL;

	return Path.substr(Ptr + 1, std::wstring::npos);
}

ForceInline std::wstring FASTCALL LowerString(std::wstring& Path)
{
	std::wstring Str = Path;
	
	for (auto& ch : Str)
		if (ch >= 'A' && ch <= 'Z')
			ch = tolower(ch);

	return Str;
}


ForceInline std::wstring FASTCALL GetFileNamePrefix(std::wstring& Path)
{
	ULONG_PTR Ptr;

	Ptr = Path.find_last_of(L".");
	if (Ptr == std::wstring::npos)
		return Path;

	return Path.substr(0, Ptr);
}


//////////////////////////////////////////////////

NAKED VOID SarCheckFake()
{
	INLINE_ASM
	{
		mov esp, ebp;
		mov eax, 1;
		ret;
	}
}

#define MAX_SECTION_COUNT 64

inline PDWORD FASTCALL GetOffset(PBYTE ModuleBase, DWORD v)
{
	IMAGE_SECTION_HEADER SectionTable[MAX_SECTION_COUNT];
	PIMAGE_DOS_HEADER    pDosHeader;
	PIMAGE_NT_HEADERS32  pNtHeader;

	pDosHeader = (PIMAGE_DOS_HEADER)ModuleBase;
	pNtHeader = (PIMAGE_NT_HEADERS32)(ModuleBase + pDosHeader->e_lfanew);
	RtlZeroMemory(SectionTable, sizeof(SectionTable));
	RtlCopyMemory(SectionTable, ModuleBase + sizeof(IMAGE_NT_HEADERS32) + pDosHeader->e_lfanew,
		pNtHeader->FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER));

	for (ULONG_PTR i = 0; i < pNtHeader->FileHeader.NumberOfSections; i++)
	{
		if (SectionTable[i].VirtualAddress <= v && v <= SectionTable[i].VirtualAddress + SectionTable[i].SizeOfRawData)
		{
			ULONG_PTR Delta = v - SectionTable[i].VirtualAddress;
			v = SectionTable[i].PointerToRawData + Delta;
			break;
		}
	}
	v += (ULONG_PTR)ModuleBase;
	return (PDWORD)v;
}

typedef struct XBundler
{
	PCHAR pDllName;
	PBYTE pBuffer;
	DWORD dwSize;
}XBundler, *PXBundler;

PXBundler pSarcheck = NULL;

API_POINTER(ZwAllocateVirtualMemory) StubZwAllocateVirtualMemory = NULL;

NTSTATUS NTAPI HookZwAllocateVirtualMemory(
	IN HANDLE ProcessHandle,
	IN OUT PVOID *BaseAddress,
	IN ULONG ZeroBits,
	IN OUT PULONG RegionSize,
	IN ULONG AllocationType,
	IN ULONG Protect
	)
{

	NTSTATUS          Status;
	PIMAGE_DOS_HEADER DosHeader;
	PIMAGE_NT_HEADERS NtHeader;
	DWORD             OldProtect;

	if ( pSarcheck &&
		!IsBadReadPtr(pSarcheck->pDllName, MAX_PATH) &&
		!IsBadReadPtr(pSarcheck->pBuffer, pSarcheck->dwSize) &&
		pSarcheck->pDllName + lstrlenA(pSarcheck->pDllName) + 5 == (PCHAR)pSarcheck->pBuffer&&
		*(PWORD)pSarcheck->pBuffer == 'ZM')
	{
		Status = StubZwAllocateVirtualMemory(ProcessHandle, BaseAddress, ZeroBits, RegionSize, AllocationType, Protect);
		
		Mp::PATCH_MEMORY_DATA p[] =
		{
			Mp::FunctionJumpVa(ZwAllocateVirtualMemory, HookZwAllocateVirtualMemory, (PVOID*)&StubZwAllocateVirtualMemory),
		};

		Mp::RestoreMemory(p, countof(p));
		Nt_ProtectMemory(NtCurrentProcess(), pSarcheck->pBuffer, pSarcheck->dwSize, PAGE_EXECUTE_READWRITE, &OldProtect);

		DosHeader = (PIMAGE_DOS_HEADER)pSarcheck->pBuffer;
		NtHeader = (PIMAGE_NT_HEADERS32)((PBYTE)DosHeader + DosHeader->e_lfanew);
		PDWORD pEntryPoint = GetOffset(pSarcheck->pBuffer, NtHeader->OptionalHeader.AddressOfEntryPoint);
		PIMAGE_EXPORT_DIRECTORY pIET = (PIMAGE_EXPORT_DIRECTORY)GetOffset(pSarcheck->pBuffer, NtHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
		*GetOffset(pSarcheck->pBuffer, pIET->AddressOfFunctions) = NtHeader->OptionalHeader.AddressOfEntryPoint + 3;


		
		//ret 0xc
		//ret 
		*pEntryPoint = 0xC3000CC2;
		//VirtualProtect(pEntryPoint, 4, OldProtect, &OldProtect);

		Nt_ProtectMemory(NtCurrentProcess(), pSarcheck->pBuffer, pSarcheck->dwSize, OldProtect, &OldProtect);

		return Status;
	}
	return StubZwAllocateVirtualMemory(ProcessHandle, BaseAddress, ZeroBits, RegionSize, AllocationType, Protect);
}

API_POINTER(VirtualAlloc) StubVirtualAlloc = NULL;

PVOID WINAPI HookVirtualAlloc(
	IN LPVOID lpAddress,
	IN SIZE_T dwSize,
	IN DWORD flAllocationType,
	IN DWORD flProtect
	)
{
	PWORD pByte = (PWORD)((PBYTE)_ReturnAddress() - 6);
	if (*pByte == 0x95FF)//call dword ptr[ebp+]
	{
		pSarcheck = (PXBundler)StubVirtualAlloc(lpAddress, dwSize, flAllocationType, flProtect);

		Mp::PATCH_MEMORY_DATA r[] =
		{
			Mp::FunctionJumpVa(VirtualAlloc, HookVirtualAlloc, &StubVirtualAlloc),
		};

		Mp::RestoreMemory(r, countof(r));

		Mp::PATCH_MEMORY_DATA p[] =
		{
			Mp::FunctionJumpVa(ZwAllocateVirtualMemory, HookZwAllocateVirtualMemory, (PVOID*)&StubZwAllocateVirtualMemory),
		};
		

		Mp::PatchMemory(p, countof(p));
		return pSarcheck;
	}
	return StubVirtualAlloc(lpAddress, dwSize, flAllocationType, flProtect);
}


API_POINTER(GetProcAddress) StubGetProcAddress = NULL;

FARPROC WINAPI HookGetProcAddress(
	IN HMODULE hModule,
	IN LPCSTR lpProcName
	)
{
	if (!IsBadReadPtr(lpProcName, 9) &&
		!strnicmp(lpProcName, "Sarcheck", 9))
	{
		Mp::PATCH_MEMORY_DATA p[] =
		{
			Mp::FunctionJumpVa(GetProcAddress, HookGetProcAddress, &StubGetProcAddress),
		};

		Mp::RestoreMemory(p, countof(p));

		return (FARPROC)SarCheckFake;
	}
	return StubGetProcAddress(hModule, lpProcName);
}


API_POINTER(GetTimeZoneInformation) StubGetTimeZoneInformation = NULL;

DWORD
WINAPI
HookGetTimeZoneInformation(
_Out_ LPTIME_ZONE_INFORMATION lpTimeZoneInformation
)
{
	static WCHAR StdName[] = L"TOKYO Standard Time";
	static WCHAR DayName[] = L"TOKYO Daylight Time";

	StubGetTimeZoneInformation(lpTimeZoneInformation);

	lpTimeZoneInformation->Bias = -540;
	lpTimeZoneInformation->StandardBias = 0;

	RtlCopyMemory(lpTimeZoneInformation->StandardName, StdName, countof(StdName) * 2);
	RtlCopyMemory(lpTimeZoneInformation->DaylightName, DayName, countof(DayName) * 2);
	return 0;
}


API_POINTER(GetLocaleInfoW) StubGetLocaleInfoW = NULL;

int
WINAPI
HookGetLocaleInfoW(
LCID     Locale,
LCTYPE   LCType,
LPWSTR lpLCData,
int      cchData)
{
	if (Locale == 0x800u && LCType == 1)
	{
		RtlCopyMemory(lpLCData, L"0411", 10);
		return 5;
	}

	return StubGetLocaleInfoW(Locale, LCType, lpLCData, cchData);
}


API_POINTER(GetFileVersionInfoSizeW) StubGetFileVersionInfoSizeW = NULL;

DWORD
APIENTRY
HookGetFileVersionInfoSizeW(
_In_        LPCWSTR lptstrFilename, /* Filename of version stamped file */
_Out_opt_ LPDWORD lpdwHandle       /* Information for use by GetFileVersionInfo */
)
{
	auto IsKernel32 = [](LPCWSTR lpFileName)->BOOL
	{
		/*
		ULONG Length = StrLengthW(lpFileName);
		if (Length < 12)
			return FALSE;

		//sarcheck.dll
		return CHAR_UPPER4W(*(PULONG64)&lpFileName[Length - 0xC]) == TAG4W('KERN') &&
			CHAR_UPPER4W(*(PULONG64)&lpFileName[Length - 0x8]) == TAG4W('AL32') &&
			CHAR_UPPER4W(*(PULONG64)&lpFileName[Length - 0x4]) == TAG4W('.DLL');
		*/

		return wcsstr(lpFileName, L"kernel32.dll") != NULL;
	};

	if (IsKernel32(lptstrFilename))
		return StubGetFileVersionInfoSizeW(L"SiglusUniversalPatch.dll", lpdwHandle);
	
	return StubGetFileVersionInfoSizeW(lptstrFilename, lpdwHandle);
}


API_POINTER(GetFileVersionInfoW) StubGetFileVersionInfoW = NULL;

BOOL
APIENTRY
HookGetFileVersionInfoW(
_In_                LPCWSTR lptstrFilename, /* Filename of version stamped file */
_Reserved_          DWORD dwHandle,          /* Information from GetFileVersionSize */
_In_                DWORD dwLen,             /* Length of buffer for info */
 LPVOID lpData            /* Buffer to place the data structure */
)
{
	auto IsKernel32 = [](LPCWSTR lpFileName)->BOOL
	{
		/*
		ULONG Length = StrLengthW(lpFileName);
		if (Length < 12)
			return FALSE;

		//sarcheck.dll
		return CHAR_UPPER4W(*(PULONG64)&lpFileName[Length - 0xC]) == TAG4W('KERN') &&
			CHAR_UPPER4W(*(PULONG64)&lpFileName[Length - 0x8]) == TAG4W('AL32') &&
			CHAR_UPPER4W(*(PULONG64)&lpFileName[Length - 0x4]) == TAG4W('.DLL');
			*/

		return wcsstr(lpFileName, L"kernel32.dll") != NULL;
	};

	if (IsKernel32(lptstrFilename))
		return StubGetFileVersionInfoW(L"SiglusUniversalPatch.dll", dwHandle, dwLen, lpData);

	return StubGetFileVersionInfoW(lptstrFilename, dwHandle, dwLen, lpData);
}




static struct SiglusConfig
{
	BOOL  PatchFontWidth;
	BOOL  PatchFontEnum;

	std::wstring                         GameFont;
	std::unordered_map<std::wstring, std::wstring> ExtensionNameReplace;
	std::unordered_map<std::wstring, std::wstring> FileMapper;

	ULONG Address;
	BYTE  Code[8];

	SiglusConfig() : PatchFontWidth(FALSE), PatchFontEnum(FALSE), Address(0xFFFFFFFF){}
} g_SiglusConfig;


/*
___:00680C1C                 mov     ds:byte_AC3CC0[esi], al
___:00680C22                 inc     esi
___:00680C23                 cmp     esi, 10000h
*/

BOOL FASTCALL PatchFontWidthTableGenerator(ULONG_PTR ReturnAddress = 0)
{
	BOOL                  Success;
	PIMAGE_DOS_HEADER     DosHeader;
	PIMAGE_NT_HEADERS32   NtHeader;
	PIMAGE_SECTION_HEADER SectionHeader;
	PBYTE                 CurrentSection;
	ULONG_PTR             CurrentSectionSize;
	ULONG_PTR             CurrentCodePtr, CodeSize;

	DosHeader     = (PIMAGE_DOS_HEADER)GetModuleHandleW(NULL);
	NtHeader      = (PIMAGE_NT_HEADERS32)((ULONG_PTR)DosHeader + DosHeader->e_lfanew);
	SectionHeader = IMAGE_FIRST_SECTION(NtHeader);
	Success       = FALSE;

	auto PatchMemoryWithNope = [](PBYTE Addr, ULONG Size)->BOOL
	{
		BOOL  Success;
		DWORD OldFlag;

		LOOP_ONCE
		{
			Success = VirtualProtect(Addr, Size, PAGE_EXECUTE_READWRITE, &OldFlag);
			if (!Success)
				return Success;

			memcpy(g_SiglusConfig.Code, Addr, 6);
			memset(Addr, 0x90, Size);
			Success = VirtualProtect(Addr, Size, OldFlag, &OldFlag);
		}
		return Success;
	};

	//PrintConsole(L"patch...\n");

	for (ULONG_PTR i = 0; i < NtHeader->FileHeader.NumberOfSections; i++, SectionHeader++)
	{
		CurrentSection     = SectionHeader->VirtualAddress + (PBYTE)DosHeader;
		CurrentSectionSize = SectionHeader->Misc.VirtualSize;
		CurrentCodePtr     = 0;

		//PrintConsoleW(L"%08x %08x %08x\n", CurrentSection, ReturnAddress, (ULONG_PTR)CurrentSection + CurrentSectionSize);
		if (IN_RANGE((ULONG_PTR)CurrentSection, ReturnAddress, (ULONG_PTR)CurrentSection + CurrentSectionSize) && !IsBadReadPtr(CurrentSection, CurrentSectionSize))
		{
			//PrintConsoleW(L"enter patch\n");
			while (CurrentCodePtr < CurrentSectionSize)
			{
				//mov mem[offset], xl
				if (CurrentSection[CurrentCodePtr] == 0x88 &&
					GetOpCodeSize32(&CurrentSection[CurrentCodePtr]) == 6)
				{
					CurrentCodePtr += 6;
					//PrintConsole(L"st [%08x] -> %02x\n", SectionHeader->VirtualAddress + (ULONG)Nt_GetExeModuleHandle() + CurrentCodePtr, CurrentSection[CurrentCodePtr]);
					//inc esi
					if (CurrentSection[CurrentCodePtr] == 0x46 &&
						GetOpCodeSize32(&CurrentSection[CurrentCodePtr]) == 1)
					{
						CurrentCodePtr += 1;
						
						//cmp esi, 1000h
						if (*(PWORD) &CurrentSection[CurrentCodePtr]     == 0xFE81 &&
							*(PDWORD)&CurrentSection[CurrentCodePtr + 2] == 0x10000 &&
							GetOpCodeSize32(&CurrentSection[CurrentCodePtr]) == 6)
						{
							CurrentCodePtr += 6;
							//ok, then patch it.
							auto PatchAddr = CurrentCodePtr - 13 + CurrentSection;
							g_SiglusConfig.Address = (ULONG)PatchAddr;
							PatchMemoryWithNope(PatchAddr, 6);

							//PrintConsole(L"Patch Address : %p\n", PatchAddr);
							return TRUE;
						}
					}
				}
				else
				{
					CodeSize = GetOpCodeSize32(&CurrentSection[CurrentCodePtr]);
					CurrentCodePtr += CodeSize;
				}
			}
		}
	}
	return Success;
}

static BOOL ExtraPatchIsInited = FALSE;


LONG NTAPI PatchExceptionHandler(EXCEPTION_POINTERS *ExceptionInfo)
{
	auto RestoreMemoryPatch = [](PBYTE Addr, ULONG Size)->BOOL
	{
		BOOL  Success;
		DWORD OldFlag;

		LOOP_ONCE
		{
			Success = VirtualProtect(Addr, Size, PAGE_EXECUTE_READWRITE, &OldFlag);
			if (!Success)
				return Success;

			memcpy(Addr, g_SiglusConfig.Code, 6);
			Success = VirtualProtect(Addr, Size, OldFlag, &OldFlag);
		}
		return Success;
	};

	if (ExceptionInfo->ExceptionRecord->ExceptionAddress == (PVOID)g_SiglusConfig.Address)
	{
		RestoreMemoryPatch((PBYTE)g_SiglusConfig.Address, 6);
		return EXCEPTION_CONTINUE_EXECUTION;
	}
	return EXCEPTION_CONTINUE_SEARCH;
}


API_POINTER(CreateFileW) StubCreateFileW = NULL;

HANDLE
WINAPI
HookCreateFileW(
_In_ LPCWSTR lpFileName,
_In_ DWORD dwDesiredAccess,
_In_ DWORD dwShareMode,
_In_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes,
_In_ DWORD dwCreationDisposition,
_In_ DWORD dwFlagsAndAttributes,
_In_opt_ HANDLE hTemplateFile
)
{ 
	ULONG        Attribute;
	std::wstring CurFileName;

	auto IsScenePack = [](LPCWSTR FileName)->BOOL
	{
		ULONG_PTR iPos = 0;

		for (INT i = 0; i < lstrlenW(FileName); i++)
		{
			if (FileName[i] == L'\\' || FileName[i] == L'/')
				iPos = i;
		}

		if (iPos != 0)
			iPos++;

		return lstrcmpW(FileName + iPos, L"Scene.pck") == 0;
	};


	auto IsGameExe = [](LPCWSTR FileName)->BOOL
	{
		ULONG_PTR iPos = 0;

		for (INT i = 0; i < lstrlenW(FileName); i++)
		{
			if (FileName[i] == L'\\' || FileName[i] == L'/')
				iPos = i;
		}

		if (iPos != 0)
			iPos++;

		return lstrcmpW(FileName + iPos, L"Gameexe.dat") == 0;
	};

	auto IsG00Image = [](LPCWSTR FileName)->BOOL
	{
		ULONG Length = lstrlenW(FileName);
		if (Length <= 4)
			return FALSE;

		if (*(PULONG64)&FileName[Length - 4] == TAG4W('.g00'))
			return TRUE;

		if (CHAR_UPPER4W(*(PULONG64)&FileName[Length - 4]) == TAG4W('.G00'))
			return TRUE;

		return FALSE;
	};

	auto IsOmvVideo = [](LPCWSTR FileName)->BOOL
	{
		ULONG Length = lstrlenW(FileName);
		if (Length <= 4)
			return FALSE;

		if (*(PULONG64)&FileName[Length - 4] == TAG4W('.omv'))
			return TRUE;

		if (CHAR_UPPER4W(*(PULONG64)&FileName[Length - 4]) == TAG4W('.OMV'))
			return TRUE;

		return FALSE;
	};

	auto&& NameExtension        = GetFileNameExtension(std::wstring(lpFileName));
	auto[FileBaseDir, FilePath] = GetFileNameAndBaseDir(std::wstring(lpFileName));
	auto&& NameExtensionLower   = LowerString(NameExtension);

	auto Finder = g_SiglusConfig.ExtensionNameReplace.find(NameExtensionLower);
	if (Finder == g_SiglusConfig.ExtensionNameReplace.end()) {
		CurFileName = lpFileName;
	}
	else
	{
		auto Replacer = Finder->second;

		if (Replacer.length() == 0) {
			CurFileName = lpFileName;
		}
		else 
		{
			if (Replacer[0] != '.') {
				Replacer = L"." + Replacer;
			}

			CurFileName = ReplaceFileNameExtension(std::wstring(lpFileName), Replacer.c_str());
			Attribute   = GetFileAttributesW(CurFileName.c_str());

			if ((Attribute == 0xffffffff) || (Attribute & FILE_ATTRIBUTE_DIRECTORY)) {
				CurFileName = lpFileName;
			}
		}
	}
	
	auto MapperFinder = g_SiglusConfig.FileMapper.find(FilePath);
	if (MapperFinder == g_SiglusConfig.FileMapper.end()) {
		CurFileName = lpFileName;
	}
	else
	{
		auto Replacer = Finder->second;
		
		if (Replacer.length() == 0) {
			CurFileName = lpFileName;
		}
		else
		{
			CurFileName = FileBaseDir + Replacer;
			Attribute = GetFileAttributesW(CurFileName.c_str());

			if ((Attribute == 0xffffffff) || (Attribute & FILE_ATTRIBUTE_DIRECTORY)) {
				CurFileName = lpFileName;
			}
		}
	}

	return StubCreateFileW(
		CurFileName.c_str(),
		dwDesiredAccess,
		dwShareMode,
		lpSecurityAttributes,
		dwCreationDisposition,
		dwFlagsAndAttributes,
		hTemplateFile
		);
}


template <class T> inline std::shared_ptr<T> AllocateMemorySafe(SIZE_T Size)
{
	return std::shared_ptr<T>(
		(T*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size),
		[](T* Ptr)
	{
		if (Ptr) {
			HeapFree(GetProcessHeap(), 0, Ptr);
		}
	});
}


std::wstring Utf8ToUtf16(std::string& Str)
{
	WCHAR WChars[500];

	RtlZeroMemory(WChars, sizeof(WChars));
	MultiByteToWideChar(CP_UTF8, 0, Str.c_str(), Str.length(), WChars, countof(WChars));

	return WChars;
}


FORCEINLINE VOID LoadPatchConfig()
{
	NTSTATUS       Status;
	NtFileDisk     File;
	ULONG          Size;
	
	g_SiglusConfig.PatchFontEnum  = TRUE;
	g_SiglusConfig.PatchFontWidth = TRUE;

	Status = File.Open(L"SiglusCfg.json");
	if (NT_FAILED(Status))
		return;

	auto Buffer = AllocateMemorySafe<BYTE>(File.GetSize32());
	if (!Buffer)
		return;

	Size = File.GetSize32();
	File.Read(Buffer.get(), Size);
	File.Close();

	Json::CharReaderBuilder rbuilder;
	Json::Reader reader;
	Json::Value  root;
	std::string  errs;
	std::string  buffer((PCSTR)Buffer.get(), Size);

	auto success = reader.parse(buffer, root, false);
	if (!success)
		return;

	if (!root.isObject())
		return;

	auto FontName = root.get("CustomFont", "SimHei").asString();
	g_SiglusConfig.GameFont = Utf8ToUtf16(FontName);

	auto ExtensionReplacerNode = root.get("ExtensionReplacer", Json::Value::null);
	if (ExtensionReplacerNode.isObject())
	{
		auto Members = ExtensionReplacerNode.getMemberNames();
		for (auto it = Members.begin(); it != Members.end(); ++it)
		{
			auto OriginalName = *it;
			auto ReplacedName = ExtensionReplacerNode[OriginalName];
			if (!ReplacedName.isString())
				continue;

			g_SiglusConfig.ExtensionNameReplace[Utf8ToUtf16(OriginalName)] = Utf8ToUtf16(ReplacedName.asString());
		}
	}


	auto FileMapperNode = root.get("FileMapper", Json::Value::null);
	if (FileMapperNode.isObject())
	{
		auto Members = FileMapperNode.getMemberNames();
		for (auto it = Members.begin(); it != Members.end(); ++it)
		{
			auto OriginalName = *it;
			auto ReplacedName = FileMapperNode[OriginalName];
			if (!ReplacedName.isString())
				continue;

			g_SiglusConfig.FileMapper[Utf8ToUtf16(OriginalName)] = Utf8ToUtf16(ReplacedName.asString());
		}
	}
}



BOOL FASTCALL BypassAlphaRom(HMODULE DllModule)
{
	PIMAGE_DOS_HEADER     DosHeader;
	PIMAGE_NT_HEADERS32   NtHeader;
	PIMAGE_SECTION_HEADER SectionHeader;
	ULONG_PTR             FirstSection;
	ULONG_PTR             FirstSize;
	ULONG                 i;

	DosHeader = (PIMAGE_DOS_HEADER)GetModuleHandleW(NULL);
	NtHeader = (PIMAGE_NT_HEADERS32)((ULONG_PTR)DosHeader + DosHeader->e_lfanew);
	SectionHeader = IMAGE_FIRST_SECTION(NtHeader);
	FirstSection = SectionHeader->VirtualAddress + (ULONG_PTR)DosHeader;
	FirstSize = SectionHeader->Misc.VirtualSize;
	i = 0;

	while (i++ < NtHeader->FileHeader.NumberOfSections)
	{
		if (*(PDWORD)&SectionHeader++->Name[1] == 'crsr')
		{
			while (*(PDWORD)&SectionHeader->Name[1] != 'tadi'&&
				*(PWORD)&SectionHeader->Name[5] != 'a'&&
				++i < NtHeader->FileHeader.NumberOfSections)//Ѱ��.idata��
			{
				SectionHeader++;
			}

			if (*(PDWORD)&SectionHeader->Name[1] == 'ttes'&&
				*(PWORD)&SectionHeader->Name[5] == 'ce')//settec ---AlphaROM
			{
				//hGFNA = InstallHookStub(GetModuleFileNameA, My_GetModuleFileNameA);
			}

			if (i < NtHeader->FileHeader.NumberOfSections)
			{
				Mp::PATCH_MEMORY_DATA p[] =
				{
					Mp::FunctionJumpVa(VirtualAlloc, HookVirtualAlloc, &StubVirtualAlloc),
				};

				Mp::PatchMemory(p, countof(p));
			}
			else
			{
				DWORD dwOld;
				VirtualProtect((PVOID)FirstSection, FirstSize, PAGE_EXECUTE_READWRITE, &dwOld);

				Mp::PATCH_MEMORY_DATA p[] =
				{
					Mp::FunctionJumpVa(GetProcAddress, HookGetProcAddress, &StubGetProcAddress),
				};

				Mp::PatchMemory(p, countof(p));
			}
			break;
		}
	}

	Mp::PATCH_MEMORY_DATA p[] =
	{
		Mp::FunctionJumpVa(GetTimeZoneInformation,  HookGetTimeZoneInformation,  &StubGetTimeZoneInformation),
		Mp::FunctionJumpVa(GetLocaleInfoW,          HookGetLocaleInfoW,          &StubGetLocaleInfoW),
		Mp::FunctionJumpVa(GetFileVersionInfoSizeW, HookGetFileVersionInfoSizeW, &StubGetFileVersionInfoSizeW),
		Mp::FunctionJumpVa(GetFileVersionInfoW,     HookGetFileVersionInfoW,     &StubGetFileVersionInfoW),
	};

	return NT_SUCCESS(Mp::PatchMemory(p, countof(p)));
}


API_POINTER(FindWindowW) StubFindWindowW = NULL;

HWND
WINAPI
HookFindWindowW(
_In_opt_ LPCWSTR lpClassName,
_In_opt_ LPCWSTR lpWindowName)
{
	//PrintConsoleW(L"%s %s\n", lpClassName, lpWindowName);
	return StubFindWindowW(lpClassName, lpWindowName);
}

VOID BypassDebuggerCheck()
{
	Mp::PATCH_MEMORY_DATA p[] =
	{
		Mp::FunctionJumpVa(FindWindowW, HookFindWindowW, &StubFindWindowW)
	};

	Mp::PatchMemory(p, countof(p));
}


API_POINTER(GetUserNameA) StubGetUserNameA = NULL;

BOOL WINAPI HookGetUserNameA(
	_Out_   LPSTR  lpBuffer,
	_Inout_ LPDWORD lpnSize
)
{
	ULONG_PTR  ReturnAddress, OpSize;
	DWORD      OldProtect;

	//PrintConsoleW(L"get user name..........\n");

	INLINE_ASM
	{
		mov eax, [ebp];  
		mov ebx, [eax + 4]; //ret addr
		mov ReturnAddress, ebx;
	}

	//PrintConsoleW(L"%08x\n", ReturnAddress);

	//find the first 'jnz' 
	OpSize = 0;
	for(ULONG_PTR i = 0; i < 0x30;)
	{
		OpSize = GetOpCodeSize32((PBYTE)(ReturnAddress + i));
		if (OpSize == 2 && ((PBYTE)(ReturnAddress + i))[0] == 0x75) //short jump
		{
			VirtualProtect((PBYTE)(ReturnAddress + i), 2, PAGE_EXECUTE_READWRITE, &OldProtect);
			((PBYTE)(ReturnAddress + i))[0] = 0xB0;
			((PBYTE)(ReturnAddress + i))[1] = 0x01;
			VirtualProtect((PBYTE)(ReturnAddress + i), 2, OldProtect, &OldProtect);
			//PrintConsoleW(L"patch..........\n");
			break;
		}
		i += OpSize;
	}

	return StubGetUserNameA(lpBuffer, lpnSize);
}


BOOL BypassDummyCheck()
{
	Mp::PATCH_MEMORY_DATA p[] = 
	{
		Mp::FunctionJumpVa(GetUserNameA, HookGetUserNameA, &StubGetUserNameA)
	};

	return NT_SUCCESS(Mp::PatchMemory(p, countof(p)));
}


API_POINTER(CreateFontIndirectW) StubCreateFontIndirectW = NULL;

HFONT WINAPI HookCreateFontIndirectW(_In_ CONST LOGFONTW *lplf)
{
	LOGFONTW Font;
	
	RtlCopyMemory(&Font, lplf, sizeof(LOGFONTW));
	if (g_SiglusConfig.GameFont.length())
	{
		//fixed oob write 
		RtlCopyMemory(
			Font.lfFaceName,
			g_SiglusConfig.GameFont.c_str(), 
			min((g_SiglusConfig.GameFont.length() + 1) * sizeof(WCHAR), sizeof(Font.lfFaceName))
		);
	}
	return StubCreateFontIndirectW(&Font);
}


API_POINTER(EnumFontFamiliesExW) StubEnumFontFamiliesExW = NULL;

typedef struct _XFONT_CALLBACK
{
	PVOID         Param;
	FONTENUMPROCW CallBack;
} XFONT_CALLBACK, *PXFONT_CALLBACK;


int NTAPI GenerateFontCallback(LOGFONTW *lpLogFont, CONST TEXTMETRICW *lpMetric, DWORD dwFlags, LPARAM lParam)
{
	PXFONT_CALLBACK Param;

	Param                = (PXFONT_CALLBACK)lParam;
	lpLogFont->lfCharSet = SHIFTJIS_CHARSET;

	return Param->CallBack(lpLogFont, lpMetric, dwFlags, (LPARAM)Param->Param);
}

int WINAPI HookEnumFontFamiliesExW(HDC hdc, LPLOGFONTW lpLogfont, FONTENUMPROCW lpProc, LPARAM lParam, DWORD dwFlags)
{
	XFONT_CALLBACK Param;

	Param.CallBack = lpProc;
	Param.Param    = (PVOID)lParam;

	return StubEnumFontFamiliesExW(hdc, lpLogfont, (FONTENUMPROCW)GenerateFontCallback, (LPARAM)&Param, dwFlags);
}

BOOL FASTCALL Initialize(HMODULE DllModule)
{
	//AllocConsole();
	BypassDebuggerCheck();

	Nt_LoadLibrary(L"ADVAPI32.DLL");

	Mp::PATCH_MEMORY_DATA p[] =
	{
		Mp::FunctionJumpVa(CreateFileW,         HookCreateFileW,         &StubCreateFileW),
		Mp::FunctionJumpVa(CreateFontIndirectW, HookCreateFontIndirectW, &StubCreateFontIndirectW),
		Mp::FunctionJumpVa(EnumFontFamiliesExW, HookEnumFontFamiliesExW, &StubEnumFontFamiliesExW)
	};
	
	BypassAlphaRom(DllModule);
	BypassDummyCheck();

	return NT_SUCCESS(Mp::PatchMemory(p, countof(p)));
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD Reason, LPVOID lpReserved)
{
	switch (Reason)
	{
	case DLL_PROCESS_ATTACH:
		DisableThreadLibraryCalls(hModule);
		LoadPatchConfig();
		return Initialize(hModule);

	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}


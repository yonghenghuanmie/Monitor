#pragma once

#undef KdPrintEx
#ifdef DBG
#define KdPrintEx(ComponentId,Level,Format,...) DbgPrintEx(ComponentId,Level,Format,__VA_ARGS__)
#else
#define KdPrintEx(ComponentId,Level,Format,...)
#undef ASSERT
#define ASSERT(exp) if(exp)
#endif // DBG

NTKERNELAPI int swprintf(wchar_t *buffer,const wchar_t *format,...);

NTKERNELAPI NTSTATUS ObQueryNameString(
	_In_      PVOID                    Object,
	_Out_opt_ POBJECT_NAME_INFORMATION ObjectNameInfo,
	_In_      ULONG                    Length,
	_Out_     PULONG                   ReturnLength
);

NTKERNELAPI
NTSTATUS
ObReferenceObjectByName(
	IN PUNICODE_STRING ObjectName,
	IN ULONG Attributes,
	IN PACCESS_STATE PassedAccessState OPTIONAL,
	IN ACCESS_MASK DesiredAccess OPTIONAL,
	IN POBJECT_TYPE ObjectType,
	IN KPROCESSOR_MODE AccessMode,
	IN OUT PVOID ParseContext OPTIONAL,
	OUT PVOID *Object
);

NTSTATUS
ObCreateObject (
	IN KPROCESSOR_MODE ProbeMode,
	IN POBJECT_TYPE ObjectType,
	IN POBJECT_ATTRIBUTES ObjectAttributes OPTIONAL,
	IN KPROCESSOR_MODE OwnershipMode,
	IN OUT PVOID ParseContext OPTIONAL,
	IN ULONG ObjectBodySize,
	IN ULONG PagedPoolCharge,
	IN ULONG NonPagedPoolCharge,
	OUT PVOID *Object
);

typedef struct _AUX_ACCESS_DATA {
	PPRIVILEGE_SET PrivilegesUsed;
	GENERIC_MAPPING GenericMapping;
	ACCESS_MASK AccessesToAudit;
	ULONG Reserve;                            //unknow...  
} AUX_ACCESS_DATA,*PAUX_ACCESS_DATA;

NTSTATUS
SeCreateAccessState(
	IN PACCESS_STATE AccessState,
	IN PAUX_ACCESS_DATA AuxData,
	IN ACCESS_MASK DesiredAccess,
	IN PGENERIC_MAPPING GenericMapping OPTIONAL
);

typedef struct _SYSTEM_MODULE_INFORMATION_ENTRY {
	ULONG Unknow1;
	ULONG Unknow2;
	ULONG Unknow3;
	ULONG Unknow4;
	PVOID64 Base;
	ULONG Size;
	ULONG Flags;
	USHORT Index;
	USHORT NameLength;
	USHORT LoadCount;
	USHORT ModuleNameOffset;
	char ImageName[256];
} SYSTEM_MODULE_INFORMATION_ENTRY,*PSYSTEM_MODULE_INFORMATION_ENTRY;

typedef struct _SYSTEM_MODULE_INFORMATION {
	ULONG Count;//内核中以加载的模块的个数
	SYSTEM_MODULE_INFORMATION_ENTRY Module[1];
} SYSTEM_MODULE_INFORMATION,*PSYSTEM_MODULE_INFORMATION;

NTKERNELAPI NTSTATUS ZwQuerySystemInformation (
	_In_      ULONG		SystemInformationClass,
	_Inout_   PVOID		SystemInformation,
	_In_      ULONG		SystemInformationLength,
	_Out_opt_ PULONG	ReturnLength
);
#define SystemModuleInformation 11

typedef struct _PEB_LDR_DATA_EX
{
	ULONG Length;
	BOOLEAN Initialized;
	PVOID SsHandle;
	LIST_ENTRY InLoadOrderModuleList;
	LIST_ENTRY InMemoryOrderModuleList;
	LIST_ENTRY InInitializationOrderModuleList;
}PEB_LDR_DATA_EX,*PPEB_LDR_DATA_EX;

typedef struct _CURDIR {
	UNICODE_STRING DosPath;
	PVOID Handle;
}CURDIR,*PCURDIR;

typedef struct _RTL_DRIVE_LETTER_CURDIR {
	USHORT Flags;
	USHORT Length;
	ULONG TimeStamp;
	STRING DosPath;
}RTL_DRIVE_LETTER_CURDIR,*PRTL_DRIVE_LETTER_CURDIR;

typedef struct _RTL_USER_PROCESS_PARAMETERS{
	ULONG MaximumLength;
	ULONG Length;
	ULONG Flags;
	ULONG DebugFlags;
	PVOID ConsoleHandle;
	ULONG ConsoleFlags;
	PVOID StandardInput;
	PVOID StandardOutput;
	PVOID StandardError;
	CURDIR CurrentDirectory;
	UNICODE_STRING DllPath;
	UNICODE_STRING ImagePathName;
	UNICODE_STRING CommandLine;
	PVOID Environment;
	ULONG StartingX;
	ULONG StartingY;
	ULONG CountX;
	ULONG CountY;
	ULONG CountCharsX;
	ULONG CountCharsY;
	ULONG FillAttribute;
	ULONG WindowFlags;
	ULONG ShowWindowFlags;
	UNICODE_STRING WindowTitle;
	UNICODE_STRING DesktopInfo;
	UNICODE_STRING ShellInfo;
	UNICODE_STRING RuntimeData;
	RTL_DRIVE_LETTER_CURDIR CurrentDirectores[32];
}RTL_USER_PROCESS_PARAMETERS,*PRTL_USER_PROCESS_PARAMETERS;

typedef struct _PEB {
	UCHAR InheritedAddressSpace;
	UCHAR ReadImageFileExecOptions;
	UCHAR BeingDebugged;
	UCHAR SpareBool;
	PVOID Mutant;
	PVOID ImageBaseAddress;
	PPEB_LDR_DATA_EX Ldr;
	PRTL_USER_PROCESS_PARAMETERS  ProcessParameters;
	UCHAR Reserved4[104];
	PVOID Reserved5[52];
	PVOID PostProcessInitRoutine;
	PVOID Reserved7;
	UCHAR Reserved6[128];
	ULONG SessionId;
}PEB;

NTKERNELAPI  PPEB  PsGetProcessPeb(PEPROCESS Process);

NTKERNELAPI struct _OBJECT_TYPE **IoDriverObjectType;

typedef unsigned short WORD;

typedef struct _IMAGE_DOS_HEADER {      // DOS .EXE header
	WORD   e_magic;                     // Magic number
	WORD   e_cblp;                      // Bytes on last page of file
	WORD   e_cp;                        // Pages in file
	WORD   e_crlc;                      // Relocations
	WORD   e_cparhdr;                   // Size of header in paragraphs
	WORD   e_minalloc;                  // Minimum extra paragraphs needed
	WORD   e_maxalloc;                  // Maximum extra paragraphs needed
	WORD   e_ss;                        // Initial (relative) SS value
	WORD   e_sp;                        // Initial SP value
	WORD   e_csum;                      // Checksum
	WORD   e_ip;                        // Initial IP value
	WORD   e_cs;                        // Initial (relative) CS value
	WORD   e_lfarlc;                    // File address of relocation table
	WORD   e_ovno;                      // Overlay number
	WORD   e_res[4];                    // Reserved words
	WORD   e_oemid;                     // OEM identifier (for e_oeminfo)
	WORD   e_oeminfo;                   // OEM information; e_oemid specific
	WORD   e_res2[10];                  // Reserved words
	LONG   e_lfanew;                    // File address of new exe header
} IMAGE_DOS_HEADER,*PIMAGE_DOS_HEADER;

typedef struct CREATEFILE
{
	KEVENT Kevent;
	NTSTATUS Status;
}CREATEFILE;

typedef struct _READFILE
{
	KEVENT Kevent;
	NTSTATUS Status;
	FILE_OBJECT *FileObject;
	LARGE_INTEGER ByteOffset;
	unsigned long Length;
}READFILE;

typedef struct WRITEFILE
{
	KEVENT Kevent;
	NTSTATUS Status;
}WRITEFILE;

typedef struct _MOUNTVOLUME
{
	KEVENT Kevent;
	NTSTATUS Status;
	DEVICE_OBJECT *LogicalVolume;
	DEVICE_OBJECT *FileSystemDevice;
}MOUNTVOLUME;

typedef struct _SURPRISEREMOVAL
{
	KEVENT Kevent;
	NTSTATUS Status;
}SURPRISEREMOVAL;

typedef struct Record Record;
typedef struct unordered_map unordered_map;

typedef struct GLOBAL
{
	DEVICE_OBJECT *interactive;
	int fs_count;
	MDL *mdl;
	volatile bool pass;
	Record *head;
	int index;
	KSPIN_LOCK write_lock;
	MDL *mdl_query;
	Query *query;
	KSPIN_LOCK query_lock;
	unordered_map *map;
	KSPIN_LOCK map_lock;
	NPAGED_LOOKASIDE_LIST lookaside;
	volatile long wait_number;
	KSPIN_LOCK wait_lock;
	PEPROCESS process;
}GLOBAL;

typedef struct _DEVICEEXTENSION
{
	DEVICE_OBJECT *Next;
	DEVICE_OBJECT *Target;
	bool IsControl;
	int Drive;
#ifdef DBG
	GLOBAL *Global;
#endif // DBG

}DEVICEEXTENSION;

typedef struct USERDATA
{
	IsPE ispe;
}USERDATA;

void InitializeFastIoDispatch(FAST_IO_DISPATCH *FastIoDispatch);
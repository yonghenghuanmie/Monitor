#pragma once

#ifndef __cplusplus
typedef unsigned char bool;
#define true	1
#define false	0
#endif // !__cplusplus

#ifndef MAX_PATH
#define MAX_PATH          260
#endif // !MAX_PATH

#define STARTMONITOR	CTL_CODE(FILE_DEVICE_UNKNOWN,0x800,METHOD_NEITHER,FILE_ANY_ACCESS)
#define STOPMONITOR		CTL_CODE(FILE_DEVICE_UNKNOWN,0x801,METHOD_NEITHER,FILE_ANY_ACCESS)

typedef enum OperateType
{
	Read,Write
}OperateType;

typedef enum IsPE
{
	unknown,pe,notpe
}IsPE;

typedef struct Record
{
#ifdef __cplusplus
	Record():complete(false){}
#endif // __cplusplus

	LARGE_INTEGER file_time;
	wchar_t process_name[MAX_PATH];
	wchar_t name[MAX_PATH];
	OperateType operate_type;
	long long offset;
	unsigned long length;
	IsPE isPE;
	volatile bool complete;
}Record;

typedef enum Allow
{
	allow_unknown,
	allow_true,
	allow_false,
	allow_success,
	allow_fail
}Allow;

typedef struct Query
{
	Record record;
	volatile Allow allow;
	volatile bool waiting;
	char *mapped;
}Query;

#define QueueLength 0x100
#define QueryQueueLength 0x10
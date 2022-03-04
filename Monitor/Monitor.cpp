#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <locale>
#include <codecvt>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <algorithm>
#include <functional>
#include <unordered_set>
#include <unordered_map>
#include <tchar.h>
#include <windows.h>
#include <sql.h>
#include <sqlext.h>
#include "jansson.h"
#include "VtFile.h"
#include "VtResponse.h"
#include "ControlCode.h"
#include "ODBC.h"
#include "Monitor.h"
#include "UI.h"

std::_tstring ConvertToString(int ErrorCode)
{
	const TCHAR *str=nullptr;
	//some system-defined error codes require some parameters
	FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,ErrorCode,
		MAKELANGID(LANG_ENGLISH,SUBLANG_ENGLISH_US),(TCHAR*)&str,0,nullptr);
	std::_tstring temp=str;
	LocalFree((HLOCAL)str);
	return temp;
}

inline TIMESTAMP_STRUCT ConvertToTimeStamp(const LARGE_INTEGER &time)
{
	FILETIME file_time={time.LowPart,(unsigned long)time.HighPart},local_time;
	FileTimeToLocalFileTime(&file_time,&local_time);
	SYSTEMTIME system_time;
	FileTimeToSystemTime(&local_time,&system_time);
	return{(SQLSMALLINT)system_time.wYear,system_time.wMonth,system_time.wDay,system_time.wHour,
		system_time.wMinute,system_time.wSecond,system_time.wMilliseconds};
}

inline auto ConvertToWide(const char *external)
{
	size_t length=std::strlen(external);
	const char *from_next;
	wchar_t *internal=new wchar_t[length],*to_next;
	auto result=std::use_facet<std::codecvt<wchar_t,char,std::mbstate_t>>(std::locale()).
		in(std::mbstate_t(),external,external+length,from_next,internal,internal+length,to_next);
	if(result==std::codecvt_base::ok)
	{
		auto deleter=[](wchar_t *p){delete[] p;};
		return std::unique_ptr<wchar_t,decltype(deleter)>(internal,deleter);
	}
	throw std::runtime_error(std::string("failed to call ConvertToWide.error code:")+std::to_string(result));
}

#if _MSC_VER>1900	//>Visual C++ 14

inline auto ConvertToUTF8(const wchar_t *internal)
{
	size_t length=std::wcslen(internal);
	const char16_t *from_next;
	char *external=new char[length*2],*to_next;
	auto result=std::use_facet<std::codecvt_utf8<char16_t>>(std::locale()).
		out(std::mbstate_t(),(char16_t*)internal,(char16_t*)internal+length,from_next,external,external+length,to_next);
	if(result==std::codecvt_base::ok)
	{
		auto deleter=[](char *p){delete[] p;};
		return std::unique_ptr<char,decltype(deleter)>(external,deleter);
	}
	throw std::runtime_error(std::string("failed to call ConvertToUTF8.error code:")+std::to_string(result));
}

#else

inline auto ConvertToUTF8(const std::wstring &str)
{
	char *buffer=new char[str.size()*2];
	if(WideCharToMultiByte(CP_UTF8,0,str.c_str(),(int)str.size(),buffer,(int)str.size()*2,nullptr,nullptr)==0)
		throw std::runtime_error("failed to call WideCharToMultiByte.");
	auto deleter=[](char *p){delete[] p;};
	return std::unique_ptr<char,decltype(deleter)>(buffer,deleter);
}

#endif

inline wchar_t* ConvertToEnum(OperateType op)
{
	switch(op)
	{
		case Read:
			return L"Read";
		case Write:
			return L"Write";
	}
	return nullptr;
}

inline wchar_t* ConvertToEnum(IsPE ispe)
{
	switch(ispe)
	{
		case unknown:
			return L"unknown";
		case pe:
			return L"true";
		case notpe:
			return L"false";
	}
	return nullptr;
}

StateMachine::StateMachine()
{
	state=NotInatalled;
	odbc.ConnectToDatabase((SQLCHAR*)"filesystem",(SQLCHAR*)"root",(SQLCHAR*)"xianjian2012");
	hSCM=OpenSCManager(nullptr,nullptr,SC_MANAGER_CONNECT|SC_MANAGER_CREATE_SERVICE);
	if(hSCM)
	{
		hService=OpenService(hSCM,_T("FilterDriver"),SERVICE_QUERY_STATUS|SERVICE_START|SERVICE_STOP|DELETE);
		if(hService)
		{
			state=Installed;
			SERVICE_STATUS ServiceStatus;
			if(QueryServiceStatus(hService,&ServiceStatus))
			{
				if(ServiceStatus.dwCurrentState==SERVICE_RUNNING)
				{
					state=Running;
				}
			}
			else
				ShowError(QueryServiceStatus);
		}
		else if(GetLastError()!=ERROR_SERVICE_DOES_NOT_EXIST)
			ShowError(OpenService);
	}
	else
		ShowError(OpenSCManager);
}

inline StateMachine::~StateMachine()
{
	if(state==Watching)
		StopWatching();
	if(hSCM)
		CloseServiceHandle(hSCM);
	if(hService)
		CloseServiceHandle(hService);
	if(hDevice!=INVALID_HANDLE_VALUE)
		CloseHandle(hDevice);
}

inline void StateMachine::ListOperation()
{
	std::_tcout<<_T("Current state:")<<StateStr[state]<<std::endl;
	std::_tcout<<_T("Please enter the following number:")<<std::endl;
	for(auto &i:opstr[state])
		std::_tcout<<i<<std::endl;
}

inline void StateMachine::GetOperation()
{
	int op;
	if(std::cin>>op)
	{
		if(state!=Watching&&op==0)
			quit=true;
		else try
		{
			(this->*map[opstr[state].at(op-1)])();
		}
		catch(const std::out_of_range &e)
		{
			e.what();
		}
	}
}

inline void StateMachine::InstallDriver()
{
	TCHAR *memory=(TCHAR*)std::malloc(MAX_PATH);
	GetModuleFileName(nullptr,memory,MAX_PATH);
	std::_tstring str=memory;
	std::free(memory);
	hService=CreateService(hSCM,_T("FilterDriver"),_T("FilterDriver"),SERVICE_QUERY_STATUS|SERVICE_START|SERVICE_STOP|DELETE
		,SERVICE_FILE_SYSTEM_DRIVER,SERVICE_AUTO_START,SERVICE_ERROR_NORMAL,(str.substr(0,str.rfind('\\')+1)+_T("FilterDriver.sys")).c_str(),
		nullptr,nullptr,nullptr,nullptr,nullptr);
	if(hService)
	{
		state=Installed;
		SERVICE_STATUS ServiceStatus;
		if(QueryServiceStatus(hService,&ServiceStatus))
		{
			if(ServiceStatus.dwCurrentState==SERVICE_RUNNING)
				state=Running;
		}
		else
			ShowError(QueryServiceStatus);
	}
	else
		ShowError(CreateService);
}

inline void StateMachine::UninstallDriver()
{
	if(DeleteService(hService))
	{
		state=NotInatalled;
		CloseServiceHandle(hService);
		hService=nullptr;
	}
	else
		ShowError(DeleteService);
}

inline void StateMachine::StartService()
{
	if(::StartService(hService,0,nullptr))
		state=Running;
	else
		ShowError(StartService);
}

inline void StateMachine::StopService()
{
	SERVICE_STATUS ServiceStatus;
	if(ControlService(hService,SERVICE_CONTROL_STOP,&ServiceStatus))
	{
		state=Installed;
		CloseHandle(hDevice);
		hDevice=INVALID_HANDLE_VALUE;
	}
	else
		ShowError(ControlService);
}

inline void StateMachine::GetDevice()
{
	hDevice=CreateFile(_T("\\\\.\\Monitor"),GENERIC_ALL,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
	if(hDevice==INVALID_HANDLE_VALUE)
		ShowError(CreateFile);
}

inline void StateMachine::StartWatching()
{
	if(hDevice==INVALID_HANDLE_VALUE)
		GetDevice();
	if(hDevice!=INVALID_HANDLE_VALUE)
	{
		head=new Record[QueueLength];
		query=new Query[QueryQueueLength];
		DWORD temp;
		if(DeviceIoControl(hDevice,STARTMONITOR,query,QueryQueueLength*sizeof(Query),head,QueueLength*sizeof(Record),&temp,nullptr))
		{
			state=Watching;
			thread=std::thread(&StateMachine::ReadDevice,this);
			query_thread=std::thread(&StateMachine::QueryThead,this);
		}
		else
			ShowError(DeviceIoControl);
	}
}

inline void StateMachine::StopWatching()
{
	DWORD temp;
	if(DeviceIoControl(hDevice,STOPMONITOR,nullptr,0,nullptr,0,&temp,nullptr))
	{
		state=Running;
		thread.join();
		delete[] head;
		head=nullptr;
		query_thread.join();
		delete[] query;
		query=nullptr;
		WriteProfile();
		white_list.clear();
		black_list.clear();
	}
	else
		ShowError(DeviceIoControl);
}

void StateMachine::ReadDevice()
{
	try
	{
		int index=0;
		Record *record=&head[index];
		while(state==Watching)
		{
			if(record->complete)
			{
				InsertToDatabase(record);
				record->complete=false;
				if(++index==QueueLength)
					index=0;
				record=&head[index];
			}
		}
		while(record->complete)
		{
			InsertToDatabase(record);
			record->complete=false;
			if(++index==QueueLength)
				index=0;
			record=&head[index];
		}
	}
	catch(const std::exception &e)
	{
		DWORD temp;
		if(DeviceIoControl(hDevice,STOPMONITOR,nullptr,0,nullptr,0,&temp,nullptr))
		{
			state=Running;
			delete[] head;
			head=nullptr;
		}
		else
			ShowError(DeviceIoControl);
		std::cout<<"\n"<<e.what()<<"\n\n";
		ListOperation();
	}
}

void StateMachine::InsertToDatabase(Record * record)
{
	mutex.lock();
	odbc.Execute<7>((SQLCHAR*)"insert into filesystem.record values(?,?,?,?,?,?,?);",ConvertToTimeStamp(record->file_time),
		(wchar_t*)record->process_name,(wchar_t*)record->name,ConvertToEnum(record->operate_type),record->offset,record->length,ConvertToEnum(record->isPE));
	mutex.unlock();
}

auto StateMachine::VirusTotal(Query *q,const char* &sha1)
{
	auto *virus_total=VtFile_new();
	VtFile_setApiKey(virus_total,"968e3bc6d33c79c2b957696cf53b3f7c9c607411ee623e67dd3b57d52f8986e4");
	std::pair<size_t,size_t> result={0,0};
	if(VtFile_scanMemBuf(virus_total,ConvertToUTF8(q->record.process_name).get(),(unsigned char*)q->mapped,q->record.length,nullptr)==0)
	{
		auto *response=VtFile_getResponse(virus_total);
		sha1=VtResponse_getString(response,"sha1");
		VtFile_report(virus_total,sha1);
		VtResponse_put(&response);
		response=VtFile_getResponse(virus_total);
		int response_code;
		if(VtResponse_getResponseCode(response,&response_code)==0&&response_code==1)
		{
			json_t *json=json_object_get(VtResponse_getJanssonObj(response),"scans");
			result={0,json_object_size(json)};
			const char *key;
			json_t *value;
			json_object_foreach(json,key,value)
				if(json_is_false(json_object_get(value,"detected")))
					++result.first;
			std::free((void*)sha1);
			sha1=nullptr;
		}
		VtResponse_put(&response);
	}
	VtFile_put(&virus_total);
	return result;
}

void StateMachine::Interactive(Query *q)
{
	Allow result;
	if(q->allow==allow_unknown)
	{
		const char *sha1=nullptr;
		UI ui(q,VirusTotal(q,sha1),sha1);
		ui.DialogBox();
		result=q->allow=(ui.result?allow_true:allow_false);
		if(ui.always)
		{
			set_mutex.lock();
			if(result==allow_true)
				white_list.insert(q->record.process_name);
			else
				black_list.insert(q->record.process_name);
			set_mutex.unlock();
		}
	}
	else
		result=q->allow;
	if(result==allow_true)
		while(q->allow==result);
	if(q->allow==allow_success)
		InsertToDatabase(&q->record);
	q->record.complete=false;
}

void StateMachine::QueryThead()
{
	int i=0;
	ReadProfile();
	while(state==Watching)
	{
		Query *q=&query[i];
		if(q->record.complete)
		{
			if(q->waiting==false)
			{
				set_mutex.lock();
				if(white_list.count(q->record.process_name))
					q->allow=allow_true;
				else if(black_list.count(q->record.process_name))
					q->allow=allow_false;
				else
					q->allow=allow_unknown;
				set_mutex.unlock();
				q->waiting=true;
				std::thread(&StateMachine::Interactive,this,q).detach();
			}
		}
		if(++i==QueryQueueLength)
			i=0;
	}
	for(i=0;i<QueryQueueLength;++i)
		while(query[i].record.complete);
}

void StateMachine::ReadProfile()
{
	std::wifstream file("profile.cfg");
	if(file)
	{
		decltype(white_list) *ptr=nullptr;
		std::wstring str;
		while(std::getline(file,str))
		{
			std::wstring lower;
			std::transform(str.begin(),str.end(),std::back_inserter(lower),[]{int(*p)(int)=std::tolower;return p;}());
			if(lower==L"[white_list]")
				ptr=&white_list;
			else if(lower==L"[black_list]")
				ptr=&black_list;
			else if(ptr)
				ptr->insert(str);
		}
		file.close();
	}
}

void StateMachine::WriteProfile()
{
	std::wofstream file("profile.cfg");
	if(file)
	{
		if(white_list.size())
		{
			file<<L"[white_list]"<<std::endl;
			for(auto &i:white_list)
				file<<i<<std::endl;
		}
		if(black_list.size())
		{
			file<<L"[black_list]"<<std::endl;;
			for(auto &i:black_list)
				file<<i<<std::endl;
		}
		file.close();
	}
}

int main()
{
	try
	{
		StateMachine sm;
		while(!sm)
		{
			sm.ListOperation();
			sm.GetOperation();
		}
	}
	catch(const std::exception &e)
	{
		std::cout<<e.what()<<std::endl;
		std::system("pause");
	}
	return 0;
}
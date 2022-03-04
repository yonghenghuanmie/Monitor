#pragma once

namespace std
{
#ifdef _UNICODE
	using _tstring=wstring;
	auto &_tcout=wcout;
#else
	using _tstring=string;
	auto &_tcout=cout;
#endif // _UNICODE
}

#define ShowError(func) std::_tcout<<#func<<_T(":")<<ConvertToString(GetLastError())<<std::endl;

class StateMachine
{
public:
	StateMachine();
	~StateMachine();
	void ListOperation();
	void GetOperation();
	operator bool()
	{
		return quit;
	}
private:
	void InstallDriver();
	void UninstallDriver();
	void StartService();
	void StopService();
	void GetDevice();
	void StartWatching();
	void StopWatching();
	void ReadDevice();
	void InsertToDatabase(Record *record);
	auto VirusTotal(Query *q,const char* &sha1);
	void Interactive(Query *q);
	void QueryThead();
	void ReadProfile();
	void WriteProfile();

	volatile enum
	{
		NotInatalled,
		Installed,
		Running,
		Watching,
		StateEnd
	}state;
	volatile bool quit=false;
	SC_HANDLE hSCM=nullptr;
	SC_HANDLE hService=nullptr;
	HANDLE hDevice=INVALID_HANDLE_VALUE;
	Record *head=nullptr;
	Query *query=nullptr;
	std::thread thread;
	std::thread query_thread;
	std::unordered_set<std::wstring> white_list,black_list;
	std::mutex mutex,set_mutex;
	ODBC odbc;
	const TCHAR *StateStr[StateEnd]={_T("NotInatalled"),_T("Installed"),_T("Running"),_T("Watching")};
	std::vector<std::vector<std::_tstring>> opstr=
	{
		{_T("1.install driver."),_T("0.quit")},
		{_T("1.start service."),_T("2.uninstall driver."),_T("0.quit")},
		{_T("1.start watching."),_T("2.stop service."),_T("0.quit")},
		{_T("1.stop watching.")}
	};
	std::unordered_map<std::_tstring,void (StateMachine::*)()> map=
	{
		{opstr[NotInatalled][0],&StateMachine::InstallDriver},
		{opstr[Installed][0],&StateMachine::StartService},{opstr[Installed][1],&StateMachine::UninstallDriver},
		{opstr[Running][0],&StateMachine::StartWatching},{opstr[Running][1],&StateMachine::StopService},
		{opstr[Watching][0],&StateMachine::StopWatching},
	};
};
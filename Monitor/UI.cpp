#include <thread>
#include <string>
#include <utility>
#include <windows.h>
#include <windowsx.h>
#include "jansson.h"
#include "VtFile.h"
#include "VtResponse.h"
#include "ControlCode.h"
#include "resource.h"
#include "UI.h"

#ifdef _WIN64
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls'		\
version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#else if WIN32
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls'		\
version='6.0.0.0' processorArchitecture='x86' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif // _WIN64

void UI::DialogBox()
{
	window=CreateDialogParamA(GetModuleHandle(nullptr),"dialog",nullptr,DialogProcedure,(LPARAM)this);
	MSG msg;
	while(GetMessage(&msg,nullptr,0,0))
	{
		if(IsDialogMessage(window,&msg)==false)
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
}

INT_PTR UI::DialogProcedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam)
{
	static UI *ui=nullptr;
	switch(message)
	{
		case WM_INITDIALOG:
		{
			ui=(UI*)lparam;
			ui->timer=CreateWaitableTimer(nullptr,false,nullptr);
			LARGE_INTEGER time;
			time.QuadPart=-10000000;
			SetWaitableTimer(ui->timer,&time,1000,nullptr,nullptr,false);
			std::thread(TimeProcedure,ui).detach();
			SetWindowTextW(GetDlgItem(window,IDC_PROCESSNAME),ui->query->record.process_name);
			SetWindowTextW(GetDlgItem(window,IDC_FILENAME),ui->query->record.name);
			SetWindowTextA(GetDlgItem(window,IDT_OPERATION),"WRITE");
			if(ui->sha1)
				SetWindowTextA(GetDlgItem(window,IDT_PASSED),"waiting");
			else if(ui->passed.second)
				SetWindowTextA(GetDlgItem(window,IDT_PASSED),(std::to_string(ui->passed.first)+"/"+std::to_string(ui->passed.second)).c_str());
		}
		return 1;

		case WM_COMMAND:
		{
			auto id=LOWORD(wparam);
			if(id==IDOK||id==IDNO)
			{
				ui->result=(id==IDOK)?true:false;
				ui->always=Button_GetCheck(GetDlgItem(window,IDC_ALWAYS));
				CancelWaitableTimer(ui->timer);
				CloseHandle(ui->timer);
				DestroyWindow(window);
				window=nullptr;
			}
		}
		return 1;

		case WM_DESTROY:
			PostQuitMessage(0);
			return 1;
	}
	return 0;
}

void UI::TimeProcedure(UI *ui)
{
	while(ui->time)
	{
		SetWindowTextA(GetDlgItem(ui->window,IDNO),(std::string("NO(")+std::to_string(ui->time)+"s)").c_str());
		if(ui->sha1)
		{
			VtFile_report(ui->virus_total,ui->sha1);
			auto *response=VtFile_getResponse(ui->virus_total);
			int response_code;
			if(VtResponse_getResponseCode(response,&response_code)==0&&response_code==1)
			{
				json_t *json=json_object_get(VtResponse_getJanssonObj(response),"scans");
				ui->passed={0,json_object_size(json)};
				const char *key;
				json_t *value;
				json_object_foreach(json,key,value)
					if(json_is_false(json_object_get(value,"detected")))
						++ui->passed.first;
				SetWindowTextA(GetDlgItem(ui->window,IDT_PASSED),(std::to_string(ui->passed.first)+"/"+std::to_string(ui->passed.second)).c_str());
				std::free((void*)ui->sha1);
				ui->sha1=nullptr;
			}
			VtResponse_put(&response);
		}
		WaitForSingleObject(ui->timer,INFINITE);
		--ui->time;
	}
	SendMessage(ui->window,WM_COMMAND,IDNO,0);
}
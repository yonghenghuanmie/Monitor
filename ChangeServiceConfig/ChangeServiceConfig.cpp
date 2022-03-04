#include <iostream>
#include <windows.h>

int main(int argc,char *argv[])
{
	auto hSCM=OpenSCManager(nullptr,nullptr,SC_MANAGER_CONNECT);
	if(hSCM)
	{
		auto hService=OpenServiceA(hSCM,argv[1],SERVICE_CHANGE_CONFIG);
		if(hService&&
			ChangeServiceConfig(hService,SERVICE_NO_CHANGE,SERVICE_DEMAND_START,SERVICE_NO_CHANGE,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr))
			std::cout<<"succeed"<<std::endl;
	}
	system("pause");
	return 0;
}
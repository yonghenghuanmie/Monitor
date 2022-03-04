#pragma once

class ODBC
{
public:
	~ODBC()
	{
		if(hSTMT)
			SQLFreeHandle(SQL_HANDLE_STMT,hSTMT);
		SQLDisconnect(hDBC);
		if(hDBC)
			SQLFreeHandle(SQL_HANDLE_DBC,hDBC);
		if(hENV)
			SQLFreeHandle(SQL_HANDLE_ENV,hENV);
	}

	void ConnectToDatabase(SQLCHAR *DataSource,SQLCHAR *UserName,SQLCHAR *Password)
	{
		SQLRETURN SQLReturn=SQLAllocHandle(SQL_HANDLE_ENV,SQL_NULL_HANDLE,&hENV);
		if(SQLReturn==SQL_SUCCESS||SQLReturn==SQL_SUCCESS_WITH_INFO)
		{
			SQLReturn=SQLSetEnvAttr(hENV,SQL_ATTR_ODBC_VERSION,(SQLPOINTER)SQL_OV_ODBC3,0);
			if(SQLReturn==SQL_SUCCESS||SQLReturn==SQL_SUCCESS_WITH_INFO)
			{
				SQLReturn=SQLAllocHandle(SQL_HANDLE_DBC,hENV,&hDBC);
				if(SQLReturn==SQL_SUCCESS||SQLReturn==SQL_SUCCESS_WITH_INFO)
				{
					SQLReturn=SQLConnectA(hDBC,DataSource,SQL_NTS,UserName,SQL_NTS,Password,SQL_NTS);
					if(SQLReturn==SQL_SUCCESS||SQLReturn==SQL_SUCCESS_WITH_INFO)
					{
						SQLReturn=SQLAllocHandle(SQL_HANDLE_STMT,hDBC,&hSTMT);
						if(SQLReturn==SQL_SUCCESS||SQLReturn==SQL_SUCCESS_WITH_INFO)
							return;
					}
				}
			}
		}
		throw std::logic_error("failed to connect to database.");
	}

	template<int number,class... Rests>
	void Execute(SQLCHAR *sql,Rests&&... rests)
	{
		if(std::count(sql,sql+std::strlen((char*)sql),'?')!=number)
			throw std::logic_error("The number of parameters does not match.");
		auto result=SQLPrepareA(hSTMT,sql,SQL_NTS);
		if(result!=SQL_SUCCESS&&result!=SQL_SUCCESS_WITH_INFO)
			throw std::logic_error(std::string("failed to call SQLPrepare.\nError Message:")+GetErrorMessage());
		Execute<1,number>(rests...);
	}
private:
	std::string GetErrorMessage()
	{
		SQLCHAR state[6],*str;
		SQLINTEGER error_code;
		SQLSMALLINT length;
		SQLGetDiagRecA(SQL_HANDLE_STMT,hSTMT,1,state,&error_code,nullptr,0,&length);
		str=new SQLCHAR[++length];
		SQLGetDiagRecA(SQL_HANDLE_STMT,hSTMT,1,state,&error_code,str,length,&length);
		std::string error_message=(char*)str;
		delete[] str;
		return error_message;
	}

	template<int index,int number,class T,class... Rests>
	void Execute(T &&parameter,Rests&&... rests)
	{
		static_assert(0,"please implement this type first.");
	}

	template<int index,int number,class... Rests>
	void Execute(TIMESTAMP_STRUCT &parameter,Rests&&... rests)
	{
		SQLRETURN result=SQLBindParameter(hSTMT,index,SQL_PARAM_INPUT,SQL_C_TYPE_TIMESTAMP,
			SQL_TYPE_TIMESTAMP,0,0,(SQLPOINTER)&parameter,sizeof(TIMESTAMP_STRUCT),nullptr);
		if(result!=SQL_SUCCESS&&result!=SQL_SUCCESS_WITH_INFO)
			throw std::logic_error(std::string("failed to call SQLBindParameter.\nError Message:")+GetErrorMessage());
		Execute<index+1,number>(rests...);
	}

	template<int index,int number,class... Rests>
	void Execute(wchar_t* &parameter,Rests&&... rests)
	{
		SQLLEN length=SQL_NTS,*pointer=&length;
		SQLRETURN result=SQLBindParameter(hSTMT,index,SQL_PARAM_INPUT,SQL_C_WCHAR,SQL_WVARCHAR,0,0,(SQLPOINTER)parameter,0,pointer);
		if(result!=SQL_SUCCESS&&result!=SQL_SUCCESS_WITH_INFO)
			throw std::logic_error(std::string("failed to call SQLBindParameter.\nError Message:")+GetErrorMessage());
		Execute<index+1,number>(rests...);
	}

	template<int index,int number,class... Rests>
	void Execute(int &parameter,Rests&&... rests)
	{
		SQLRETURN result=SQLBindParameter(hSTMT,index,SQL_PARAM_INPUT,SQL_C_LONG,SQL_INTEGER,0,0,(SQLPOINTER)&parameter,sizeof(int),nullptr);
		if(result!=SQL_SUCCESS&&result!=SQL_SUCCESS_WITH_INFO)
			throw std::logic_error(std::string("failed to call SQLBindParameter.\nError Message:")+GetErrorMessage());
		Execute<index+1,number>(rests...);
	}

	template<int index,int number,class... Rests>
	void Execute(unsigned long &parameter,Rests&&... rests)
	{
		SQLRETURN result=SQLBindParameter(hSTMT,index,SQL_PARAM_INPUT,SQL_C_ULONG,SQL_INTEGER,0,0,(SQLPOINTER)&parameter,sizeof(unsigned long),nullptr);
		if(result!=SQL_SUCCESS&&result!=SQL_SUCCESS_WITH_INFO)
			throw std::logic_error(std::string("failed to call SQLBindParameter.\nError Message:")+GetErrorMessage());
		Execute<index+1,number>(rests...);
	}

	template<int index,int number,class... Rests>
	void Execute(long long &parameter,Rests&&... rests)
	{
		SQLRETURN result=SQLBindParameter(hSTMT,index,SQL_PARAM_INPUT,SQL_C_SBIGINT,SQL_BIGINT,0,0,(SQLPOINTER)&parameter,sizeof(long long),nullptr);
		if(result!=SQL_SUCCESS&&result!=SQL_SUCCESS_WITH_INFO)
			throw std::logic_error(std::string("failed to call SQLBindParameter.\nError Message:")+GetErrorMessage());
		Execute<index+1,number>(rests...);
	}

	template<int index,int number>
	void Execute()
	{
		static_assert(index==number+1,"The number of parameters specified is incorrect.");
		SQLRETURN result=SQLExecute(hSTMT);
		if(result!=SQL_SUCCESS&&result!=SQL_SUCCESS_WITH_INFO)
			throw std::logic_error(std::string("failed to call SQLExecute.\nError Message:")+GetErrorMessage());
	}

	SQLHENV hENV=nullptr;
	SQLHDBC hDBC=nullptr;
	SQLHSTMT hSTMT=nullptr;
};
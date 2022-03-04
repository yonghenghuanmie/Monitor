#pragma once

#ifdef DialogBox
#undef DialogBox
#endif // DialogBox


class UI
{
public:
	UI(Query *q,std::pair<size_t,size_t> p,const char* s):query(q),passed(p),sha1(s)
	{
		if(sha1)
		{
			virus_total=VtFile_new();
			VtFile_setApiKey(virus_total,"968e3bc6d33c79c2b957696cf53b3f7c9c607411ee623e67dd3b57d52f8986e4");
		}
	}
	~UI(){ VtFile_put(&virus_total); }

	void DialogBox();
	bool result;
	bool always=false;
private:
	static INT_PTR DialogProcedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam);
	static void UI::TimeProcedure(UI *ui);
	HWND window;
	HANDLE timer;
	int time=15;
	Query *query;
	std::pair<size_t,size_t> passed;
	const char *sha1;
	VtFile *virus_total=nullptr;
};
#include "stdafx.h"
#include "TrainerWorker.h"
#include "JiYuTrainer.h"
#include "AppPublic.h"
#include "NtHlp.h"
#include "PathHelper.h"
#include "StringHlp.h"
#include "MsgCenter.h"
#include "StringSplit.h"
#include "DriverLoader.h"
#include "KernelUtils.h"
#include "SysHlp.h"
#include "SettingHlp.h"
#include "RegHlp.h"

extern JTApp *currentApp;
extern LoggerInternal * currentLogger;

TrainerWorkerInternal * TrainerWorkerInternal::currentTrainerWorker = nullptr;

extern NtQuerySystemInformationFun NtQuerySystemInformation;

using namespace std;

#define TIMER_RESET_PID 40115
#define TIMER_CK 40116

PSYSTEM_PROCESSES current_system_process = NULL;

TrainerWorkerInternal::TrainerWorkerInternal()
{
	currentTrainerWorker = this;

	//For message box center
	hMsgBoxHook = SetWindowsHookEx(
		WH_CBT,        // Type of hook 
		CBTProc,        // Hook procedure (see below)
		NULL,         // Module handle. Must be NULL (see docs)
		GetCurrentThreadId()  // Only install for THIS thread!!!
	);
}
TrainerWorkerInternal::~TrainerWorkerInternal()
{
	if (hDesktop) {
		CloseDesktop(hDesktop);
		hDesktop = NULL;
	}

	UnhookWindowsHookEx(hMsgBoxHook);

	StopInternal();
	ClearProcess();

	currentTrainerWorker = nullptr;
}

void TrainerWorkerInternal::Init()
{
	hDesktop = OpenDesktop(L"Default", 0, FALSE, DESKTOP_ENUMERATE);
	UpdateScreenSize();

	if (LocateStudentMainLocation()) currentLogger->Log(L"�Ѷ�λ������ӽ���λ�ã� %s", _StudentMainPath.c_str());
	else currentLogger->LogWarn(L"�޷���λ������ӽ���λ��");

	UpdateState();
	UpdateStudentMainInfo(false);

	InitSettings();
}
void TrainerWorkerInternal::InitSettings()
{
	SettingHlp*settings = currentApp->GetSettings();
	setAutoIncludeFullWindow = settings->GetSettingBool(L"AutoIncludeFullWindow");
	setCkInterval = currentApp->GetSettings()->GetSettingInt(L"CKInterval", 3100);
	setAllowGbTop = settings->GetSettingBool(L"AllowGbTop", false);
	if (setCkInterval < 1000 || setCkInterval > 10000) setCkInterval = 3000;

	if (_StudentMainControlled)
		SendMessageToVirus(L"hk:reset");
}
void TrainerWorkerInternal::UpdateScreenSize()
{
	screenWidth = GetSystemMetrics(SM_CXSCREEN);
	screenHeight = GetSystemMetrics(SM_CYSCREEN);
}

void TrainerWorkerInternal::Start()
{
	if (!_Running) 
	{
		_Running = true;

		//Settings
		
		SetTimer(hWndMain, TIMER_CK, setCkInterval, TimerProc);
		SetTimer(hWndMain, TIMER_RESET_PID, 2000, TimerProc);

		UpdateState();

	}
}
void TrainerWorkerInternal::Stop()
{
	if (_Running) {
		StopInternal();
		UpdateState();
	}
}
void TrainerWorkerInternal::StopInternal() {
	if (_Running) {
		_Running = false;
		KillTimer(hWndMain, TIMER_CK);
		KillTimer(hWndMain, TIMER_RESET_PID);
	}
}

void TrainerWorkerInternal::SetUpdateInfoCallback(TrainerWorkerCallback *callback)
{
	if (callback) {
		_Callback = callback;
		hWndMain = callback->GetMainHWND();
	}
}

void TrainerWorkerInternal::HandleMessageFromVirus(LPCWSTR buf)
{
	wstring act(buf);
	vector<wstring> arr;
	SplitString(act, arr, L":");
	if (arr.size() >= 2)
	{
		if (arr[0] == L"hkb")
		{
			if (arr[1] == L"succ") {
				_StudentMainControlled = true;
				currentLogger->LogInfo(L"Receive ctl success message ");
				if (_Callback) _Callback->OnBeforeSendStartConf();
				UpdateState();

				//HS 
				FAST_STR_BINDER(str, L"hs:%ld", 32, (ULONG_PTR)hWndMain);
				SendMessageToVirus(str);
			}
			else if (arr[1] == L"immck") {
				RunCk();
				currentLogger->LogInfo(L"Receive  immck message ");
			}
			else if (arr[1] == L"jyk") {
				if (arr.size() >= 3) {
					_StudentMainRunningLock = arr[2] == L"1";
					UpdateState();
				}
			}
			else if (arr[1] == L"algbtop") {
				currentLogger->LogInfo(L"Receive allow gbTop message ");
				_Callback->OnAllowGbTop();
			}
			else if (arr[1] == L"gbmfull") {
				gbFullManual = true;
				ManualFull(gbFullManual);
			}
			else if (arr[1] == L"gbmnofull") {
				if (_FakeFull) FakeFull(false);
				gbFullManual = false;
				ManualFull(gbFullManual);
			}
			else if (arr[1] == L"gbuntop") ManualTop(false);
			else if (arr[1] == L"gbtop") ManualTop(true);
			else if (arr[1] == L"showhelp" && _Callback) _Callback->OnShowHelp();
			else if (arr[1] == L"wtf") {
				//�����˴����Ŀ�꣬��������״̬
				DWORD pid = _wtol(arr[2].c_str());
				AddFilterdPid(pid);

				if (_StudentMainFileLocatedByProcess)
					_StudentMainFileLocated = false;
				_StudentMainPid = 0;

				currentLogger->Log(L"��������ֵ� StudentMain.exe");

				UpdateState();
				UpdateStudentMainInfo(false);
			}
		}
		else if (arr[0] == L"wcd")
		{
			//wwcd
			int wcdc = _wtoi(arr[1].c_str());
			if (wcdc % 20 == 0)
				currentLogger->LogInfo(L"Receive watch dog message %d ", wcdc);
		}
		else if (arr[0] == L"vback") {
			wstring strBuff = arr[1];
			if (arr.size() > 2) {
				for (UINT i = 2; i < arr.size(); i++)
					strBuff += L":" + arr[i];
			}
			currentLogger->Log(L"Receive virus echo : %s", strBuff.c_str());
		}
	}
}
void TrainerWorkerInternal::SendMessageToVirus(LPCWSTR buf)
{
	MsgCenterSendToVirus(buf, hWndMain);
}

bool TrainerWorkerInternal::Kill(bool autoWork)
{
	if (_StudentMainPid <= 4) {
		currentLogger->LogError(L"δ�ҵ�����������");
		return false;
	}
	if (_StudentMainControlled){
		bool vkill = autoWork;
		if(!vkill){
			MSGBOXPARAMSW mbp = { 0 };
			mbp.hwndOwner = hWndMain;
			mbp.lpszCaption = L"JiYuTrainer - ��ʾ";
			mbp.lpszText = L"���Ƿ�ϣ��ʹ�ò������б��ƣ�";
			mbp.cbSize = sizeof(MSGBOXPARAMSW);
			mbp.dwStyle = MB_ICONEXCLAMATION | MB_TOPMOST | MB_YESNOCANCEL;
			int drs = MessageBoxIndirect(&mbp);
			if (drs == IDCANCEL) return false;
			else if (drs == IDYES) vkill = true;
		}
		if (vkill) {
			//Stop sginal
			SendMessageToVirus(L"ss2:0");
			return true;
		}
	}

	HANDLE hProcess;
	NTSTATUS status = MOpenProcessNt(_StudentMainPid, &hProcess);
	if (!NT_SUCCESS(status)) {
		if (status == STATUS_INVALID_CID || status == STATUS_INVALID_HANDLE) {
			_StudentMainPid = 0;
			_StudentMainControlled = false;
			UpdateState();
			UpdateStudentMainInfo(!autoWork);
			return true;
		}
		else {
			currentLogger->LogError(L"�򿪽��̴���0x%08X�����ֶ�����", status);
			return false;
		}
	}
	status = MTerminateProcessNt(0, hProcess);
	if (NT_SUCCESS(status)) {
		_StudentMainPid = 0;
		_StudentMainControlled = false;
		UpdateState();
		UpdateStudentMainInfo(!autoWork);
		CloseHandle(hProcess);
		return TRUE;
	}
	else {
		if (status == STATUS_ACCESS_DENIED) goto FORCEKILL;
		else if (status != STATUS_INVALID_CID && status != STATUS_INVALID_HANDLE) {
			currentLogger->LogError(L"�������̴���0x%08X�����ֶ�����", status);
			if (!autoWork)
				MessageBox(hWndMain, L"�޷�����������ӽ��ң�����Ҫʹ�����������ֶ�����", L"JiYuTrainer - ����", MB_ICONERROR);;
			CloseHandle(hProcess);
			return false;
		}
		else if (status == STATUS_INVALID_CID || status == STATUS_INVALID_HANDLE) {
			_StudentMainPid = 0;
			_StudentMainControlled = false;
			UpdateState();
			UpdateStudentMainInfo(!autoWork);
			CloseHandle(hProcess);
			return true;
		}
	}

FORCEKILL:
	if (XDriverLoaded() && MessageBox(hWndMain, L"��ͨ�޷����������Ƿ����������������\n���������ܲ��ȶ��������á���Ҳ����ʹ�� PCHunter �Ȱ�ȫ�������ǿɱ��", L"JiYuTrainer - ��ʾ", MB_ICONEXCLAMATION | MB_YESNO | MB_TOPMOST) == IDYES)
	{
		if (KForceKill(_StudentMainPid, &status)) {
			_StudentMainPid = 0;
			_StudentMainControlled = false;
			UpdateState();
			UpdateStudentMainInfo(!autoWork);
			CloseHandle(hProcess);
			return true;
		}
		else if(!autoWork) MessageBox(hWndMain, L"����Ҳ�޷���������ʹ�� PCHunter �������ɣ�", L"����", MB_ICONEXCLAMATION);
	}
	CloseHandle(hProcess);
	return false;
}
bool TrainerWorkerInternal::KillStAuto()
{
	RunCk();
	RunResetPid();

	return Kill(true);
}
bool TrainerWorkerInternal::Rerun(bool autoWork)
{
	if (!_StudentMainFileLocated) {
		currentLogger->LogWarn(L"δ�ҵ�������ӽ���");
		if (!autoWork && _Callback)
			_Callback->OnSimpleMessageCallback(L"<h5>�����޷��ڴ˼�������ҵ�������ӽ��ң�����Ҫ�ֶ�����</h5>");
		return false;
	}
	return  SysHlp::RunApplication(_StudentMainPath.c_str(), NULL);
}
void* TrainerWorkerInternal::RunOperation(TrainerWorkerOp op) 
{
	switch (op)
	{
	case TrainerWorkerOpVirusBoom: {
		MsgCenterSendToVirus(L"ss:0", hWndMain);
		return nullptr;
	}
	case TrainerWorkerOpVirusQuit: {
		MsgCenterSendToVirus((LPWSTR)L"hk:ckend", hWndMain);
		return nullptr;
	}
	case TrainerWorkerOp1: {
		WCHAR s[300]; swprintf_s(s, L"hk:path:%s", currentApp->GetFullPath());
		MsgCenterSendToVirus(s, hWndMain);
		swprintf_s(s, L"hk:inipath:%s", currentApp->GetPartFullPath(PART_INI));
		MsgCenterSendToVirus(s, hWndMain);
		break;
	}
	case TrainerWorkerOpForceUnLoadVirus: {
		UnLoadAllVirus();
		break;
	}
	case TrainerWorkerOp2: {
		if (ReadTopDomanPassword(false))
			return (LPVOID)_TopDomainPassword.c_str();
		return nullptr;
	}
	case TrainerWorkerOp3: {
		if (ReadTopDomanPassword(true)) 
			return (LPVOID)_TopDomainPassword.c_str();
		return nullptr;
	}
	case TrainerWorkerOp4: {
		return (void*)UnLoadJiYuProtectDriver();
	}
	case TrainerWorkerOp5: {
		return (void*) UnLoadNetFilterDriver();
	}
	}
	return nullptr;
}

bool TrainerWorkerInternal::RunCk()
{
	_LastResolveWindowCount = 0;
	_LastResoveBroadcastWindow = false;
	_LastResoveBlackScreenWindow = false;
	_FirstBlackScreenWindow = false;

	EnumDesktopWindows(hDesktop, EnumWindowsProc, (LPARAM)this);

	MsgCenterSendHWNDS(hWndMain);

	return _LastResolveWindowCount > 0;
}
void TrainerWorkerInternal::RunResetPid()
{
	FlushProcess();

	//CK GET STAT DELAY
	if (_NextLoopGetCkStat) {
		_NextLoopGetCkStat = false;
		SendMessageToVirus(L"hk:ckstat");
	}

	//Find jiyu main process
	DWORD newPid = 0;
	if (LocateStudentMain(&newPid)) { //�ҵ�����

		if (_StudentMainPid != newPid)
		{
			_StudentMainPid = newPid;

			if (InstallVirus()) {
				_VirusInstalled = true;
				_NextLoopGetCkStat = true;

				currentLogger->Log(L"�� StudentMain.exe [%d] ע��DLL�ɹ�", newPid);
			}
			else  currentLogger->LogError(L"�� StudentMain.exe [%d] ע��DLLʧ��", newPid);

			currentLogger->Log(L"������ StudentMain.exe [%d]", newPid);

			UpdateState();
			UpdateStudentMainInfo(false);
		}
	}
	else { //û���ҵ�

		if (_StudentMainPid != 0)
		{
			_StudentMainPid = 0;

			currentLogger->Log(L"���������� StudentMain.exe ���˳�", newPid);

			UpdateState();
			UpdateStudentMainInfo(false);
		}

	}

	/*
	newPid = 0;
	if (LocateMasterHelper(&newPid)) {
		if (_MasterHelperPid != newPid)
		{
			_MasterHelperPid = newPid;
			if (InstallVirusForMaster()) currentLogger->Log(L"�� MasterHelper.exe [%d] ע��DLL�ɹ�", newPid);
			else  currentLogger->LogError(L"�� MasterHelper.exe [%d] ע��DLLʧ��", newPid);
		}
	}
	else {
		_MasterHelperPid = 0;
	}
	*/
}

bool TrainerWorkerInternal::FlushProcess()
{
	ClearProcess();

	DWORD dwSize = 0;
	NTSTATUS status = NtQuerySystemInformation(SystemProcessInformation, NULL, 0, &dwSize);
	if (status == STATUS_INFO_LENGTH_MISMATCH && dwSize > 0)
	{
		current_system_process = (PSYSTEM_PROCESSES)malloc(dwSize);
		status = NtQuerySystemInformation(SystemProcessInformation, current_system_process, dwSize, 0);
		if (!NT_SUCCESS(status)) {
			currentLogger->LogError2(L"NtQuerySystemInformation failed ! 0x%08X", status);
			return false;
		}
	}

	return true;
}
void TrainerWorkerInternal::ClearProcess()
{
	if (current_system_process) {
		free(current_system_process);
		current_system_process = NULL;
	}
}
bool TrainerWorkerInternal::FindProcess(LPCWSTR processName, DWORD * outPid)
{
	return false;
}
bool TrainerWorkerInternal::KillProcess(DWORD pid, bool force)
{
	HANDLE hProcess;
	NTSTATUS status = MOpenProcessNt(_StudentMainPid, &hProcess);
	if (!NT_SUCCESS(status)) {
		if (status == STATUS_INVALID_CID || status == STATUS_INVALID_HANDLE) {
			currentLogger->LogError2(L"�Ҳ������� [%d] ", pid);
			return true;
		}
		else {
			currentLogger->LogError2(L"�򿪽��� [%d] ����0x%08X�����ֶ�����", pid);
			return false;
		}
	}
	status = MTerminateProcessNt(0, hProcess);
	if (NT_SUCCESS(status)) {
		currentLogger->Log(L"���� [%d] �����ɹ�", pid);
		CloseHandle(hProcess);
		return TRUE;
	}
	else {
		if (status == STATUS_ACCESS_DENIED) {
			if (force) goto FORCEKILL;
			else currentLogger->LogError2(L"�������� [%d] ���󣺾ܾ����ʡ��ɳ���ʹ����������", pid);
			CloseHandle(hProcess);
		}
		else if (status != STATUS_INVALID_CID && status != STATUS_INVALID_HANDLE) {
			currentLogger->LogError2(L"�������� [%d] ����0x%08X�����ֶ�����", pid);
			CloseHandle(hProcess);
			return false;
		}
		else if (status == STATUS_INVALID_CID || status == STATUS_INVALID_HANDLE) {
			currentLogger->LogError2(L"�Ҳ������� [%d] ", pid);
			CloseHandle(hProcess);
			return true;
		}
	}
FORCEKILL:
	if (XDriverLoaded())
	{
		if (KForceKill(_StudentMainPid, &status)) {
			currentLogger->Log(L"���� [%d] ǿ�ƽ����ɹ�", pid);
			CloseHandle(hProcess);
			return true;
		}
		else {
			currentLogger->LogError2(L"����ǿ�ƽ������� [%d] ����0x%08X", pid);
		}
	}
	else currentLogger->Log(L"����δ���أ��޷�ǿ�ƽ�������");
	CloseHandle(hProcess);
	return false;
}
bool TrainerWorkerInternal::ReadTopDomanPassword(BOOL forceKnock)
{
	_TopDomainPassword.clear();

	HKEY hKey;
	LRESULT lastError;

	if (forceKnock) goto READ_EX;
	//��ͨע����ȡ��������4.0�汾

	WCHAR Data[32];
	if (MRegReadKeyString64And32(HKEY_LOCAL_MACHINE,
		L"SOFTWARE\\TopDomain\\e-Learning Class Standard\\1.00",
		L"SOFTWARE\\Wow6432Node\\TopDomain\\e-Learning Class Standard\\1.00", L"UninstallPasswd", Data, 32)) {
		
		if (StrEqual(Data, L"Passwd[123456]")) goto READ_EX; //6.0�Ժ��ȡ�����ˣ�����ʾPasswd[123456]�����µķ�����ȡ
		else {
			_TopDomainPassword = Data;
			_TopDomainPassword = _TopDomainPassword.substr(6, _TopDomainPassword.size() - 6);
			return true;
		}

	}
	else currentLogger->LogWarn2(L"MRegReadKeyString64And32 Failed : %s (%d)", PRINT_LAST_ERROR_STR);

	//HKEY_LOCAL_MACHINE\SOFTWARE\TopDomain\e-Learning Class\Student Knock1
READ_EX:

	lastError = RegOpenKeyEx(HKEY_LOCAL_MACHINE, SysHlp::Is64BitOS() ?
		L"SOFTWARE\\Wow6432Node\\TopDomain\\e-Learning Class\\Student" : 
		L"SOFTWARE\\TopDomain\\e-Learning Class\\Student", 0, KEY_WOW64_64KEY | KEY_READ, &hKey);
	if (lastError == ERROR_SUCCESS) {

		DWORD dwType = REG_BINARY;
		BYTE Data[120];
		DWORD cbData = 120;
		lastError = RegQueryValueEx(hKey, L"Knock1", 0, &dwType, (LPBYTE)Data, &cbData);
		if (lastError == ERROR_SUCCESS) {

			RegCloseKey(hKey);

			WCHAR ss[34] = { 0 };
			if (UnDecryptJiyuKnock(Data, cbData, ss)) {
				_TopDomainPassword = ss;
				return true;
			}
			else {
				currentLogger->LogWarn2(L"UnDecryptJiyuKnock failed !");
				return false;
			}
		}
		else currentLogger->LogWarn2(L"RegQueryValueEx Failed : %d", lastError);
		RegCloseKey(hKey);
	}
	else currentLogger->LogWarn2(L"RegOpenKeyEx Failed : %d", lastError);
	return false;
}
bool TrainerWorkerInternal::AppointStudentMainLocation(LPCWSTR fullPath) {
	if (Path::GetFileName(fullPath) != L"StudentMain.exe")	return false;
	if (Path::Exists(fullPath)) 
	{
		_StudentMainPath = fullPath;
		_StudentMainFileLocated = true;
		_StudentMainFileLocatedByProcess = false;

		currentApp->GetSettings()->SetSettingStr(L"StudentMainPath", fullPath);
		currentLogger->Log(L"�ɹ��ֶ���λ������ӽ���λ�ã� %s", fullPath);

		UpdateStudentMainInfo(false);
		return true;
	}
	return false;
}
bool TrainerWorkerInternal::LocateStudentMainLocation()
{
	//ע������ ���� ·��
	WCHAR Data[MAX_PATH];
	if (MRegReadKeyString64And32(HKEY_LOCAL_MACHINE,
		L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\e-Learning Class V6.0",
		L"SOFTWARE\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\e-Learning Class V6.0", 
		L"DisplayIcon", Data, MAX_PATH)) {

		if (Path::Exists(Data)) {
			_StudentMainPath = Data;
			_StudentMainFileLocated = true;
			return true;
		}
		else currentLogger->Log(L"��ȡע��� [DisplayIcon] �����һ����Ч�ļ�����ӽ���·�� : %s", Data);
	}
	else currentLogger->LogWarn2(L"MRegReadKeyString64And32 Failed : %s (%d)", PRINT_LAST_ERROR_STR);

	//��ȡ�û�ָ����·��
	wstring appointStudentMainPath  = currentApp->GetSettings()->GetSettingStr(L"StudentMainPath", L"", MAX_PATH);
	if (!StrEmepty(appointStudentMainPath.c_str()) && Path::Exists(appointStudentMainPath)) {
		wstring fileName = Path::GetFileName(appointStudentMainPath);
		if (StrEqual(fileName.c_str(), L"StudentMain.exe")) {
			_StudentMainPath = appointStudentMainPath;
			_StudentMainFileLocated = true;
			return true;
		}
	}

	//ֱ�ӳ��Բ���
	LPCWSTR mabeInHere[6] = {
		L"c:\\Program Files\\Mythware\\������ù���ϵͳ���V6.0 2016 ������\\StudentMain.exe",
		L"C:\\Program Files\\Mythware\\e-Learning Class\\StudentMain.exe",
		L"C:\\Program Files (x86)\\Mythware\\������ù���ϵͳ���V6.0 2016 ������\\StudentMain.exe",
		L"C:\\Program Files (x86)\\Mythware\\e - Learning Class\\StudentMain.exe",
		L"C:\\e-Learning Class\\StudentMain.exe",
		L"c:\\������ù���ϵͳ���V6.0 2016 ������\\StudentMain.exe",
	};
	for (int i = 0; i < 6; i++) {
		if (Path::Exists(mabeInHere[i])) {
			_StudentMainPath = mabeInHere[i];
			_StudentMainFileLocated = true;
			_StudentMainFileLocatedByProcess = false;
			return true;
		}
	}

	return false;
}
bool TrainerWorkerInternal::LocateStudentMain(DWORD *outFirstPid)
{
	if (current_system_process)
	{
		bool done = false;
		for (PSYSTEM_PROCESSES p = current_system_process; !done; p = PSYSTEM_PROCESSES(PCHAR(p) + p->NextEntryOffset)) {
			if (p->ImageName.Length && StrEqual(p->ImageName.Buffer, L"StudentMain.exe"))
			{
				if (CheckPidFilterd((DWORD)p->ProcessId)) continue;

				HANDLE hProcess;
				if (NT_SUCCESS(MOpenProcessNt((DWORD)p->ProcessId, &hProcess))) {
					WCHAR buffer[MAX_PATH];
					if (MGetProcessFullPathEx(hProcess, buffer)) {
						//���exe��ͬĿ¼���Ƿ���� LibTDMaster.dll ���������ų�
						PathRemoveFileSpec(buffer);
						wcscat_s(buffer, L"\\LibTDMaster.dll");
						if (!PathFileExists(buffer)) {
							AddFilterdPid((DWORD)p->ProcessId);
							currentLogger->Log(L"��ֵ� StudentMain.exe [%d]", (DWORD)p->ProcessId);
							continue;
						}

						//Exe ȷ��λ��
						if (!_StudentMainFileLocated) {
							currentLogger->Log(L"ͨ������ StudentMain.exe [%d] ��λ��λ�ã� %s", (DWORD)p->ProcessId, _StudentMainPath);
							_StudentMainPath = buffer;
							_StudentMainFileLocated = true;
							_StudentMainFileLocatedByProcess = true;
						}
					}
					CloseHandle(hProcess);
				}

				if (outFirstPid) *outFirstPid = (DWORD)p->ProcessId;

				ClearFilterdPid();
				return true;
			}
			done = p->NextEntryOffset == 0;
		}
		ClearFilterdPid();
	}
	return false;
}
bool TrainerWorkerInternal::LocateMasterHelper(DWORD *outFirstPid)
{
	if (current_system_process)
	{
		bool done = false;
		for (PSYSTEM_PROCESSES p = current_system_process; !done; p = PSYSTEM_PROCESSES(PCHAR(p) + p->NextEntryOffset)) {
			if (p->ImageName.Length && StrEqual(p->ImageName.Buffer, L"MasterHelper.exe"))
			{
				if (outFirstPid)*outFirstPid = (DWORD)p->ProcessId;
				return true;
			}
			done = p->NextEntryOffset == 0;
		}
	}
	return false;
}
bool TrainerWorkerInternal::CheckPidFilterd(DWORD pid)
{
	for (auto it = incorrectStudentMainPids.begin(); it != incorrectStudentMainPids.end(); it++) {
		if ((*it).pid == pid)
			return true;
	}
	return false;
}
void TrainerWorkerInternal::AddFilterdPid(DWORD pid)
{
	IncorrectStudentMainFilterData data;
	data.checked = true;
	data.pid = pid;
	incorrectStudentMainPids.push_back(data);
}
void TrainerWorkerInternal::ClearFilterdPid()
{
	if (current_system_process)
	{
		for (auto it = incorrectStudentMainPids.begin(); it != incorrectStudentMainPids.end(); it++) {
			auto pid = (*it).pid;
			bool founded = false;
			bool done = false;
			for (PSYSTEM_PROCESSES p = current_system_process; !done; p = PSYSTEM_PROCESSES(PCHAR(p) + p->NextEntryOffset)) {
				if ((DWORD)p->ProcessId == pid)
				{
					founded = true;
					break;
				}
				done = p->NextEntryOffset == 0;
			}
			if (!founded) 
				it = incorrectStudentMainPids.erase(it);
		}
	}
}


void TrainerWorkerInternal::UpdateStudentMainInfo(bool byUser)
{
	if (_Callback)
		_Callback->OnUpdateStudentMainInfo(_StudentMainPid > 4, _StudentMainPath.c_str(), _StudentMainPid, byUser);
}
void TrainerWorkerInternal::UpdateState()
{
	if (_Callback) 
	{
		TrainerWorkerCallback::TrainerStatus status;
		if (_StudentMainPid > 4) {
			if (_StudentMainControlled) {
				_StatusTextMain = L"�ѿ��Ƽ�����ӽ���";

				if (!_Running) {
					_StatusTextMain += L" ��������δ����";
					status = TrainerWorkerCallback::TrainerStatus::TrainerStatusStopped;
				}
				else if (_StudentMainRunningLock)
				{
					_StatusTextMore = L"��Ϊ������������ӽ���<br/>�����Է��ļ������Ĺ���";
					status = TrainerWorkerCallback::TrainerStatus::TrainerStatusControlledAndUnLocked;
				}
				else {
					_StatusTextMore = L"�����Է��ļ������Ĺ���";
					status = TrainerWorkerCallback::TrainerStatus::TrainerStatusControlled;
				}
			}
			else {
				_StatusTextMain = L"�޷����Ƽ�����ӽ���";
				if (!_Running) {
					_StatusTextMain = L"������δ����";
					_StatusTextMore = L"�����ֶ�ֹͣ������<br / >��ǰ����Լ������κβ���";
					status = TrainerWorkerCallback::TrainerStatus::TrainerStatusStopped;
				}
				else if (_VirusInstalled) {
					_StatusTextMore = L"���Ѳ��뼫�򣬵�δ��������<br / ><span style=\"color:#f41702\">��������Ҫ������������</span>";
					status = TrainerWorkerCallback::TrainerStatus::TrainerStatusUnknowProblem;
				}
				else {
					_StatusTextMore = L"������ӽ��Ҳ��벡��ʧ��<br / >����������鿴 <a id=\"link_log\">��־</a>";
					status = TrainerWorkerCallback::TrainerStatus::TrainerStatusControllFailed;
				}
			}
		}
		else {
			_StatusTextMain = L"������ӽ���δ����";
			if (!_Running) {
				_StatusTextMain = L"������ӽ���δ���� ���ҿ�����δ����";
				_StatusTextMore = L"�����ֶ�ֹͣ������<br / >��ǰ�����⼫�������";
				status = TrainerWorkerCallback::TrainerStatus::TrainerStatusStopped;
			}
			else if (_StudentMainFileLocated) {
				status = TrainerWorkerCallback::TrainerStatus::TrainerStatusNotRunning;
				_StatusTextMore = L"���ڴ˼�������ҵ�������ӽ���<br / >����Ե�� <b>�·���ť</b> ������";
			}
			else {
				status = TrainerWorkerCallback::TrainerStatus::TrainerStatusNotFound;
				_StatusTextMore = L"δ�ڴ˼�������ҵ�������ӽ���";
			}
		}

		_Callback->OnUpdateState(status, _StatusTextMain.c_str(), _StatusTextMore.c_str());
	}
}

bool TrainerWorkerInternal::InstallVirus()
{
	return InjectDll(_StudentMainPid, currentApp->GetPartFullPath(PART_HOOKER));
}
bool TrainerWorkerInternal::InstallVirusForMaster()
{
	return InjectDll(_MasterHelperPid, currentApp->GetPartFullPath(PART_HOOKER));
}
bool TrainerWorkerInternal::InjectDll(DWORD pid, LPCWSTR dllPath)
{
	HANDLE hRemoteProcess;
	//�򿪽���
	NTSTATUS ntStatus = MOpenProcessNt(pid, &hRemoteProcess);
	if (!NT_SUCCESS(ntStatus)) {
		currentLogger->LogError2(L"ע�벡��ʧ�ܣ��򿪽���ʧ�ܣ�0x%08X", ntStatus);
		return FALSE;
	}

	wchar_t *pszLibFileRemote;

	//ʹ��VirtualAllocEx������Զ�̽��̵��ڴ��ַ�ռ����DLL�ļ����ռ�
	pszLibFileRemote = (wchar_t *)VirtualAllocEx(hRemoteProcess, NULL, sizeof(wchar_t) * (lstrlen(dllPath) + 1), MEM_COMMIT, PAGE_READWRITE);

	//ʹ��WriteProcessMemory������DLL��·����д�뵽Զ�̽��̵��ڴ�ռ�
	WriteProcessMemory(hRemoteProcess, pszLibFileRemote, (void *)dllPath, sizeof(wchar_t) * (lstrlen(dllPath) + 1), NULL);

	//##############################################################################
		//����LoadLibraryA����ڵ�ַ
	PTHREAD_START_ROUTINE pfnStartAddr = (PTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandle(TEXT("Kernel32")), "LoadLibraryW");
	//(����GetModuleHandle������GetProcAddress����)

	//����Զ���߳�LoadLibraryW��ͨ��Զ���̵߳��ô����µ��߳�
	HANDLE hRemoteThread;
	if ((hRemoteThread = CreateRemoteThread(hRemoteProcess, NULL, 0, pfnStartAddr, pszLibFileRemote, 0, NULL)) == NULL)
	{
		currentLogger->LogError2(L"ע���߳�ʧ��! ����CreateRemoteThread %d", GetLastError());
		return FALSE;
	}

	// �ͷž��

	CloseHandle(hRemoteProcess);
	CloseHandle(hRemoteThread);

	return true;
}
bool TrainerWorkerInternal::UnInjectDll(DWORD pid, LPCWSTR moduleName)
{
	HANDLE hProcess;
	//�򿪽���
	NTSTATUS ntStatus = MOpenProcessNt(pid, &hProcess);
	if (!NT_SUCCESS(ntStatus)) {
		currentLogger->LogError2(L"ж�ز���ʧ�ܣ��򿪽���ʧ�ܣ�0x%08X", ntStatus);
		return FALSE;
	}
	DWORD pszLibFileRemoteSize = sizeof(wchar_t) * (lstrlen(moduleName) + 1);
	wchar_t *pszLibFileRemote;
	//ʹ��VirtualAllocEx������Զ�̽��̵��ڴ��ַ�ռ����DLL�ļ����ռ�
	pszLibFileRemote = (wchar_t *)VirtualAllocEx(hProcess, NULL, pszLibFileRemoteSize, MEM_COMMIT, PAGE_READWRITE);
	//ʹ��WriteProcessMemory������DLL��·����д�뵽Զ�̽��̵��ڴ�ռ�
	WriteProcessMemory(hProcess, pszLibFileRemote, (void *)moduleName, pszLibFileRemoteSize, NULL);

	DWORD dwHandle;
	DWORD dwID;
	LPVOID pFunc = GetProcAddress(GetModuleHandle(TEXT("Kernel32")), "GetModuleHandleW");
	HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)pFunc, pszLibFileRemote, 0, &dwID);
	if (!hThread) {
		currentLogger->LogError2(L"ж�ز���ʧ�ܣ�����Զ���߳�ʧ�ܣ�%d", GetLastError());
		return FALSE;
	}

	// �ȴ�GetModuleHandle�������
	WaitForSingleObject(hThread, INFINITE);
	// ���GetModuleHandle�ķ���ֵ
	GetExitCodeThread(hThread, &dwHandle);
	// �ͷ�Ŀ�����������Ŀռ�
	VirtualFreeEx(hProcess, pszLibFileRemote, pszLibFileRemoteSize, MEM_DECOMMIT);
	CloseHandle(hThread);
	// ʹĿ����̵���FreeLibrary��ж��DLL
	pFunc = GetProcAddress(GetModuleHandle(TEXT("Kernel32")), "FreeLibrary"); ;
	hThread = CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)pFunc, (LPVOID)dwHandle, 0, &dwID);
	if (!hThread) {
		currentLogger->LogError2(L"ж�ز���ʧ�ܣ�����Զ���߳�ʧ�ܣ�%d", GetLastError());
		return FALSE;
	}
	
	// �ȴ�FreeLibraryж�����
	WaitForSingleObject(hThread, INFINITE);
	CloseHandle(hThread);
	CloseHandle(hProcess);

	return true;
}
bool TrainerWorkerInternal::UnLoadAllVirus()
{
	if (_MasterHelperPid > 4) {
		if (UnInjectDll(_MasterHelperPid, L"JiYuTrainerHooks.dll"))
			currentLogger->Log(L"��ǿ��ж�� MasterHelper ����");
		//KillProcess(_MasterHelperPid, false);
	}
	if (_StudentMainPid > 4)
		if (UnInjectDll(_StudentMainPid, L"JiYuTrainerHooks.dll"))
			currentLogger->Log(L"��ǿ��ж�� StudentMain ����");

	return false;
}
bool TrainerWorkerInternal::UnLoadNetFilterDriver()
{
	return MUnLoadDriverServiceWithMessage(L"TDNetFilter");
}
bool TrainerWorkerInternal::UnLoadJiYuProtectDriver()
{
	return false;
}

bool TrainerWorkerInternal::SwitchFakeFull()
{
	if (_FakeFull) { 
		_FakeFull = false; 
		SendMessageToVirus(L"hk:fkfull:false");
		FakeFull(_FakeFull);
	}
	else if(_StudentMainRunningLock) { 
		_FakeFull = true; 
		SendMessageToVirus(L"hk:fkfull:true");
		FakeFull(_FakeFull);
	}

	return _FakeFull;
}
void TrainerWorkerInternal::FakeFull(bool fk) {
	if (_CurrentBroadcastWnd)
	{
		if (fk) {
			SetWindowLong(_CurrentBroadcastWnd, GWL_EXSTYLE, GetWindowLong(_CurrentBroadcastWnd, GWL_EXSTYLE) | WS_EX_TOPMOST);
			SetWindowLong(_CurrentBroadcastWnd, GWL_STYLE, GetWindowLong(_CurrentBroadcastWnd, GWL_STYLE) ^ (WS_BORDER | WS_OVERLAPPEDWINDOW));
			SetWindowPos(_CurrentBroadcastWnd, HWND_TOPMOST, 0, 0, screenWidth, screenHeight, SWP_SHOWWINDOW);
			SendMessage(_CurrentBroadcastWnd, WM_SIZE, 0, MAKEWPARAM(screenWidth, screenHeight));
			/*HWND jiYuGBDeskRdWnd = FindWindowExW(currentGbWnd, NULL, NULL, L"TDDesk Render Window");
			if (jiYuGBDeskRdWnd != NULL) {

			}*/
			_FakeBroadcastFull = true;
			currentLogger->Log(L"�����㲥���ڼ�װȫ��״̬");
		}
		else {
			_FakeBroadcastFull = false;
			FixWindow(_CurrentBroadcastWnd, (LPWSTR)L"��Ļ�㲥");
			int w = (int)((double)screenWidth * (3.0 / 4.0)), h = (int)((double)screenHeight * (double)(4.0 / 5.0));
			SetWindowPos(_CurrentBroadcastWnd, 0, (screenWidth - w) / 2, (screenHeight - h) / 2, w, h, SWP_NOZORDER | SWP_SHOWWINDOW);
			currentLogger->Log(L"ȡ���㲥���ڼ�װȫ��״̬");
		}
	}
	if (_CurrentBlackScreenWnd) 
	{
		if (fk) {
			SetWindowLong(_CurrentBlackScreenWnd, GWL_EXSTYLE, GetWindowLong(_CurrentBlackScreenWnd, GWL_EXSTYLE) | WS_EX_TOPMOST);
			SetWindowLong(_CurrentBlackScreenWnd, GWL_STYLE, GetWindowLong(_CurrentBlackScreenWnd, GWL_STYLE) ^ (WS_BORDER | WS_OVERLAPPEDWINDOW));
			SetWindowPos(_CurrentBlackScreenWnd, HWND_TOPMOST, 0, 0, screenWidth, screenHeight, SWP_SHOWWINDOW);
			SendMessage(_CurrentBlackScreenWnd, WM_SIZE, 0, MAKEWPARAM(screenWidth, screenHeight));
			SendMessage(_CurrentBlackScreenWnd, WM_SIZE, 0, MAKEWPARAM(screenWidth, screenHeight));
			_FakeBlackScreenFull = true;
			currentLogger->Log(L"�����������ڼ�װȫ��״̬");
		}
		else {
			_FakeBlackScreenFull = false;
			FixWindow(_CurrentBlackScreenWnd, (LPWSTR)L"BlackScreen Window");
			currentLogger->Log(L"ȡ���������ڼ�װȫ��״̬");
		}
	}
	if (!fk && !_CurrentBlackScreenWnd && !_CurrentBroadcastWnd && (_FakeBlackScreenFull || _FakeBroadcastFull)) {
		_FakeBroadcastFull = false;
		_FakeBlackScreenFull = false;
	}
}
void TrainerWorkerInternal::ManualFull(bool fk)
{
	currentLogger->Log(L"receive ManualFull %s", fk ? L"true" : L"false");
	if (_CurrentBroadcastWnd) 
	{
		if (fk) {
			SetWindowLong(_CurrentBroadcastWnd, GWL_EXSTYLE, GetWindowLong(_CurrentBroadcastWnd, GWL_EXSTYLE) | WS_EX_TOPMOST);
			SetWindowLong(_CurrentBroadcastWnd, GWL_STYLE, GetWindowLong(_CurrentBroadcastWnd, GWL_STYLE) ^ (WS_BORDER | WS_OVERLAPPEDWINDOW) | WS_SYSMENU);
			SetWindowPos(_CurrentBroadcastWnd, HWND_TOPMOST, 0, 0, screenWidth, screenHeight, SWP_SHOWWINDOW);
			SendMessage(_CurrentBroadcastWnd, WM_SIZE, 0, MAKEWPARAM(screenWidth, screenHeight));
		}
		else {
			FixWindow(_CurrentBroadcastWnd, (LPWSTR)L"��Ļ�㲥");
			int w = (int)((double)screenWidth * (3.0 / 4.0)), h = (int)((double)screenHeight * (double)(4.0 / 5.0));
			SetWindowPos(_CurrentBroadcastWnd, 0, (screenWidth - w) / 2, (screenHeight - h) / 2, w, h, SWP_NOZORDER | SWP_SHOWWINDOW);
		}
	}
}
void TrainerWorkerInternal::ManualTop(bool fk)
{
	currentLogger->Log(L"receive ManualTop %s", fk ? L"true" : L"false");
	if (_CurrentBroadcastWnd)
	{
		if (fk) {
			SetWindowLong(_CurrentBroadcastWnd, GWL_EXSTYLE, GetWindowLong(_CurrentBroadcastWnd, GWL_EXSTYLE) | WS_EX_TOPMOST);
			SetWindowPos(_CurrentBroadcastWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_SHOWWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		}
		else {
			SetWindowLong(_CurrentBroadcastWnd, GWL_EXSTYLE, GetWindowLong(_CurrentBroadcastWnd, GWL_EXSTYLE) ^ WS_EX_TOPMOST);
			SetWindowPos(_CurrentBroadcastWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_SHOWWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		}
	}
}
bool TrainerWorkerInternal::ChecIsJIYuWindow(HWND hWnd, LPDWORD outPid, LPDWORD outTid) {
	if (_StudentMainPid == 0) return false;
	DWORD pid = 0, tid = GetWindowThreadProcessId(hWnd, &pid);
	if (outPid) *outPid = pid;
	if (outTid) *outTid = tid;
	return pid == _StudentMainPid;
}
bool TrainerWorkerInternal::CheckIsTargetWindow(LPWSTR text, HWND hWnd) {
	bool b = false;
	
	if (StrEqual(text, L"BlackScreen Window")) {
		b = true;
		_LastResoveBlackScreenWindow = true;
		if (!_FirstBlackScreenWindow) {
			_FirstBlackScreenWindow = true;
			if (_Callback) _Callback->OnResolveBlackScreenWindow();
		}
		_CurrentBlackScreenWnd = hWnd;
		if (_FakeBlackScreenFull) return false;
	}
	else if (CheckWindowTextIsGb(text)) {
		b = true;
		_LastResoveBroadcastWindow = true;
		_CurrentBroadcastWnd = hWnd;
		if (_FakeBroadcastFull) return false;
	}
	return b;
}
void TrainerWorkerInternal::FixWindow(HWND hWnd, LPWSTR text)
{
	_LastResolveWindowCount++;

	LONG oldLong = GetWindowLong(hWnd, GWL_EXSTYLE);

	if (StrEqual(text, L"BlackScreen Window"))
	{
		oldLong = GetWindowLong(hWnd, GWL_EXSTYLE);
		{
			SetWindowLong(hWnd, GWL_EXSTYLE, oldLong ^ WS_EX_APPWINDOW | WS_EX_NOACTIVATE);
			SetWindowPos(hWnd, 0, 20, 20, 90, 150, SWP_NOZORDER | SWP_DRAWFRAME | SWP_NOACTIVATE);
			ShowWindow(hWnd, SW_HIDE);
		}
	}
	//Un top
	if (CheckWindowTextIsGb(text))
	{
		if (!setAllowGbTop && (oldLong & WS_EX_TOPMOST) == WS_EX_TOPMOST)
		{
			SetWindowLong(hWnd, GWL_EXSTYLE, oldLong ^ WS_EX_TOPMOST);
			SetWindowPos(hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		}
		if (!gbFullManual) 
		{
			//Set border and sizeable
			SetWindowLong(hWnd, GWL_STYLE, GetWindowLong(hWnd, GWL_STYLE) | (WS_BORDER | WS_OVERLAPPEDWINDOW));
		}
	}
	else if(setAutoIncludeFullWindow)
	{
		if ((oldLong & WS_EX_TOPMOST) == WS_EX_TOPMOST)
		{
			SetWindowLong(hWnd, GWL_EXSTYLE, oldLong ^ WS_EX_TOPMOST);
			SetWindowPos(hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		}

		//Set border and sizeable
		SetWindowLong(hWnd, GWL_STYLE, GetWindowLong(hWnd, GWL_STYLE) | (WS_BORDER | WS_OVERLAPPEDWINDOW));

	}

	SetWindowPos(hWnd, 0, 0, 0, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOMOVE | SWP_DRAWFRAME | SWP_NOACTIVATE);
}
bool TrainerWorkerInternal::CheckWindowTextIsGb(const wchar_t* text) {
	return StringHlp::StrContainsW(text, L"�㲥", nullptr) || StringHlp::StrContainsW(text, L"��ʾ", nullptr)
		|| StringHlp::StrContainsW(text, L"����", nullptr)
		|| StringHlp::StrEqualW(text, L"��Ļ�ݲ��Ҵ���");
}

BOOL CALLBACK TrainerWorkerInternal::EnumWindowsProc(HWND hWnd, LPARAM lParam)
{
	TrainerWorkerInternal *self =(TrainerWorkerInternal *)lParam;
	if (IsWindowVisible(hWnd) && self->ChecIsJIYuWindow(hWnd, NULL, NULL)) {
		WCHAR text[50];
		GetWindowText(hWnd, text, 50);
		if (StrEqual(text, L"JiYu Trainer Virus Window")) return TRUE;

		RECT rc;
		GetWindowRect(hWnd, &rc);
		if (self->CheckIsTargetWindow(text, hWnd)) {
			//JiYu window
			MsgCenteAppendHWND(hWnd);
			self->FixWindow(hWnd, text);
		}
		else if (self->setAutoIncludeFullWindow && rc.top == 0 && rc.left == 0 && rc.right == self->screenWidth && rc.bottom == self->screenHeight) {
			//Full window
			MsgCenteAppendHWND(hWnd);
			self->FixWindow(hWnd, text);
		}
	}
	return TRUE;
}
VOID CALLBACK TrainerWorkerInternal::TimerProc(HWND hWnd, UINT message, UINT_PTR iTimerID, DWORD dwTime)
{
	if (currentTrainerWorker != nullptr) 
	{
		if (iTimerID == TIMER_RESET_PID) {
			currentTrainerWorker->RunResetPid();
		}
		if (iTimerID == TIMER_CK) {
			currentTrainerWorker->RunCk();
		}
	}
}
LRESULT CALLBACK TrainerWorkerInternal::CBTProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode < 0)
		return CallNextHookEx(currentTrainerWorker->hMsgBoxHook, nCode, wParam, lParam);
	switch (nCode)
	{
	case HCBT_ACTIVATE: {
		// ����wParam�о���message box�ľ��
		HWND hWnd = (HWND)wParam;
		HWND hWndOwner = GetWindow(hWnd, GW_OWNER);

		// �����Ѿ�����message box�ľ�������������ǾͿ��Զ���message box��!
		if (hWndOwner && hWndOwner  == currentTrainerWorker->hWndMain)
		{
			//�����ڸ����ھ���
			RECT rect; GetWindowRect(hWnd, &rect);
			RECT rectParent; GetWindowRect(hWndOwner, &rectParent);
			rect.left = ((rectParent.right - rectParent.left) - (rect.right - rect.left)) / 2 + rectParent.left;
			rect.top = ((rectParent.bottom - rectParent.top) - (rect.bottom - rect.top)) / 2 + rectParent.top;
			SetWindowPos(hWnd, 0, rect.left, rect.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
		}
		return 0;
	}
	}
	// Call the next hook, if there is one
	return CallNextHookEx(currentTrainerWorker->hMsgBoxHook, nCode, wParam, lParam);
}

bool UnDecryptJiyuKnock(BYTE* Data, DWORD cbData, WCHAR* ss)
{
	//������Ĵ���
	__try {
		DWORD v5; // esi
		DWORD v6; // ecx
		BYTE *v7; // eax
		DWORD v8; // edx
		BYTE *i;
		v5 = cbData;
		v6 = cbData >> 2;
		v7 = Data;
		if (cbData >> 2)
		{
			v8 = cbData >> 2;
			do
			{
				*(DWORD *)v7 ^= 0x50434C45u;
				v7 += 4;
				--v8;
			} while (v8);
		}
		for (i = Data; v6; --v6)
		{
			*(DWORD *)i ^= 0x454C4350u;
			i += 4;
		}
		WORD v4[34];
		v4[0] = 0;
		memset(&v4[1], 0, 0x40u);

		int a1 = (int)&v4;

		int v13; // edi
		BYTE *v14; // eax
		__int16 v15; // cx
		v13 = a1 - Data[0];
		v14 = &Data[Data[0]];
		do
		{
			v15 = *(WORD *)v14;
			*(WORD *)&v14[v13 - (DWORD)Data] = *(WORD *)v14;
			v14 += 2;
		} while (v15);


		for (int i = 0; i < 32; i++) ss[i] = v4[i];
		return true;
	}
	__except (1) {
		return false;
	}
}

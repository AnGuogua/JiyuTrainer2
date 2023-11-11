#pragma once

#include "Logger.h"
#include "SettingHlp.h"
#include "TrainerWorker.h" 
#include "JyUdpAttack.h" 

#define CURRENT_VERSION "1.7.6.0515" 

#define FAST_STR_BINDER(str, fstr, size, ...) WCHAR str[size]; swprintf_s(str, fstr, __VA_ARGS__)

#define PART_INI -1
#define PART_MAIN 0
#define PART_LOG 1
#define PART_HOOKER 3
#define PART_DRIVER 4
#define PART_SCITER 5


enum EXTRACT_RES {
	ExtractUnknow,
	ExtractCreateFileError,
	ExtractWriteFileError,
	ExtractReadResError,
	ExtractSuccess
};
enum AppOperation {
	AppOperation1,
	AppOperation2,
	AppOperation3,
	AppOperationKShutdown,
	AppOperationKReboot,
	AppOperationUnLoadDriver,
	AppOperationForceLoadDriver,
};

class JTApp
{
public:

	/*
		ж��
	*/
	virtual void UnInstall() {}

	/*
		�ͷ�ģ����Դ���ļ�
		[resModule] ��Դ����ģ��
		[resId] ��Դid
		[resType] ��Դ����
		[extractTo] �ļ�·��
	*/
	virtual EXTRACT_RES InstallResFile(HINSTANCE resModule, LPWSTR resId, LPCWSTR resType, LPCWSTR extractTo) { return EXTRACT_RES::ExtractUnknow; }

	//��������в����Ƿ����ĳ������
	virtual bool IsCommandExists(LPCWSTR cmd) { return false; }

	//��ȡ�����в�������
	virtual LPWSTR *GetCommandLineArray() { return nullptr; }
	//��ȡ�����в��������С
	virtual int GetCommandLineArraySize() { return 0; }

	virtual LPCWSTR MakeFromSourceArg(LPCWSTR arg) { return nullptr; }



	virtual void LoadDriver() {}

	/*
		���������в����������е�λ��
		[szArgList] �����в�������
		[argCount] �����в��������С
		[arg] Ҫ���������в���
		[����] ����ҵ����������������򷵻�-1
	*/
	virtual int FindArgInCommandLine(LPWSTR *szArgList, int argCount, const wchar_t * arg) { return 0; }

	/*
		���г���
	*/
	virtual int Run(int nCmdShow) { return 0; }
	virtual int GetResult() { return 0; }
	virtual void Exit(int code) {  }

	virtual LPVOID RunOperation(AppOperation op) { return nullptr; }

	/*
		��ȡ��ǰ���� HINSTANCE
	*/
	virtual HINSTANCE GetInstance() { return nullptr; }

	/*
		��ȡ��������λ��
		[partId] ��������
	*/
	virtual LPCWSTR GetPartFullPath(int partId) { return nullptr; }

	//��ȡ��ǰ��������·��
	virtual LPCWSTR GetFullPath() { return nullptr; }
	//��ȡ��ǰ����Ŀ¼
	virtual LPCWSTR GetCurrentDir() { return nullptr; }
	virtual LPCWSTR GetSourceInstallerPath() { return nullptr; }
	
	virtual int GetAppShowCmd() { return 0; };
	virtual bool GetAppIsHiddenMode() { return false; };

	virtual LPCWSTR GetStartupErr() { return nullptr; }

	virtual JyUdpAttack* GetJyUdpAttack() { return nullptr; };
	virtual Logger* GetLogger() { return nullptr; };
	virtual SettingHlp* GetSettings() { return nullptr; };
	virtual bool GetSelfProtect() { return false; }
	virtual TrainerWorker* GetTrainerWorker() { return nullptr; };
	virtual void*GetSciterAPI() { return nullptr; }
};
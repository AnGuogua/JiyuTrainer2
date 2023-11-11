#include "stdafx.h"
#include "CAutoLock.h"

CAutoLock::CAutoLock()
{
    InitializeCriticalSection(&m_Section);
    //Lock();������õ�ʱ��ֻ���������󣬿��Բ��ֶ������ٽ������˳��ٽ���
}
CAutoLock::~CAutoLock()
{
    DeleteCriticalSection(&m_Section);
    //UnLock();
}
void CAutoLock::Lock()
{
    EnterCriticalSection(&m_Section);
}
void CAutoLock::UnLock()
{
    LeaveCriticalSection(&m_Section);
}
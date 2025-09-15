#pragma once

class CommunicationPort
{
public:

	static CommunicationPort* Instance()
	{
        static CommunicationPort singleton;
        return &singleton;
	}

	NTSTATUS InitFilterPort(
		_In_ PFLT_FILTER Filter);

	void FinalizeFilterPort();

	static NTSTATUS PortConnectNotify(
		_In_ PFLT_PORT ClientPort,
		_In_opt_ PVOID ServerPortCookie,
		_In_reads_bytes_opt_(SizeOfContext) PVOID ConnectionContext,
		_In_ ULONG SizeOfContext,
		_Outptr_result_maybenull_ PVOID* ConnectionPortCookie);

	static void PortDisconnectNotify(_In_opt_ PVOID ConnectionCookie);

	static NTSTATUS PortMessageNotify(
		_In_opt_ PVOID PortCookie,
		_In_reads_bytes_opt_(InputBufferLength) PVOID InputBuffer,
		_In_ ULONG InputBufferLength,
		_Out_writes_bytes_to_opt_(OutputBufferLength, *ReturnOutputBufferLength) PVOID OutputBuffer,
		_In_ ULONG OutputBufferLength,
		_Out_ PULONG ReturnOutputBufferLength);

	NTSTATUS SendOutputMessage(_In_ PortMessageType type, _In_ LPCWSTR formatString, ...);
	NTSTATUS SendSectionMessage(_In_ HANDLE Sectionhandle, 
		_In_ ULONG FileSizeBytes,
		_Out_ NTSTATUS& UserReply);

    ULONG GetConnectedPID() const { return m_ConnectedPID; }

	// Caller needs to call ZwClose() on this handle
	NTSTATUS GetConnectedProcessHandle(HANDLE& hProcess) const;
	// Caller needs to call ObDereferenceObject on object
    NTSTATUS GetConnectedProcessObject(PEPROCESS& process) const;

    bool IsConnected() const { return m_ClientPort != nullptr; }

private:

	PFLT_PORT m_Port = {};
	PFLT_PORT m_ClientPort = {};
	PFLT_FILTER m_Filter = {};

	PEPROCESS m_ConnectedProcess = {};
	ULONG     m_ConnectedPID = {};

	CommunicationPort() = default;
};

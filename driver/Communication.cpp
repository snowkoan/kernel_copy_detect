#include "pch.h"

#include "Communication.h"
#include "pooltag.h"

NTSTATUS CommunicationPort::InitFilterPort(
    _In_ PFLT_FILTER Filter)
{
	NTSTATUS status = STATUS_INTERNAL_ERROR;

    do
    {
		UNICODE_STRING name = RTL_CONSTANT_STRING(FILTER_PORT_NAME);
		PSECURITY_DESCRIPTOR sd;

		status = FltBuildDefaultSecurityDescriptor(&sd, FLT_PORT_ALL_ACCESS);
		if (!NT_SUCCESS(status))
		{
			break;
		}

		OBJECT_ATTRIBUTES attr;
		InitializeObjectAttributes(&attr, &name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, nullptr, sd);

		status = FltCreateCommunicationPort(Filter, 
			&Instance()->m_Port, 
			&attr, 
			nullptr,
			PortConnectNotify, 
			PortDisconnectNotify, 
			PortMessageNotify, 
			1);

		FltFreeSecurityDescriptor(sd);

		if (!NT_SUCCESS(status))
		{
			break;
		}

		Instance()->m_Filter = Filter;

    } while (false);
    
	return status;
}

void CommunicationPort::FinalizeFilterPort()
{
	if (m_Port)
	{
		FltCloseCommunicationPort(m_Port);
        m_Port = nullptr;
	}
}

// Rudimentary output message sender.
NTSTATUS CommunicationPort::SendOutputMessage(_In_ PortMessageType type, _In_ LPCWSTR formatString, ...)
{
	NTSTATUS status = STATUS_CONNECTION_INVALID;

	if (IsConnected())
	{
		va_list args;
		va_start(args, formatString);

		auto msg = (PortMessage*)ExAllocatePool2(
			POOL_FLAG_PAGED, 
			COMMUNICATION_BUFFER_LEN, 
			COMMUNINCATION_POOLTAG);

		if (msg)
		{
			msg->type = type;

			UNICODE_STRING tmpString = { 0, 
				COMMUNICATION_BUFFER_LEN - FIELD_OFFSET(PortMessage, stringMsg.data), 
				reinterpret_cast<wchar_t*>(msg->stringMsg.data) };
			if (NT_SUCCESS(status = RtlUnicodeStringVPrintf(&tmpString, formatString, args)))
			{
				msg->stringMsg.dataLenBytes = tmpString.Length;

				// LARGE_INTEGER timeout;
				// timeout.QuadPart = -10000 * 100; // 100 msec
				status = FltSendMessage(m_Filter,
					&m_ClientPort,
					msg,
					sizeof(PortMessage) + msg->stringMsg.dataLenBytes,
					nullptr,
					nullptr,
					nullptr);
			}

			ExFreePool(msg);
		}
		else
		{
			status = STATUS_NO_MEMORY;
		}
	}

	return status;
}

NTSTATUS CommunicationPort::GetConnectedProcessHandle(HANDLE& Process) const
{
	Process = nullptr;
    auto status = STATUS_CONNECTION_INVALID;

    if (IsConnected())
    {
		 // Access type is kernel - this should always be permitted, according to MSDN
		 status = ObOpenObjectByPointer(
			m_ConnectedProcess,
			OBJ_KERNEL_HANDLE,
			NULL,
			PROCESS_ALL_ACCESS,
			*PsProcessType,
			KernelMode,
			&Process);
    }
    
	return status;
}

NTSTATUS CommunicationPort::GetConnectedProcessObject(PEPROCESS& process) const
{
    process = nullptr;
    auto status = STATUS_CONNECTION_INVALID;

    if (IsConnected())
    {
        ObReferenceObject(m_ConnectedProcess);
        process = m_ConnectedProcess;
        status = STATUS_SUCCESS;
    }
    return status;
}

NTSTATUS CommunicationPort::SendSectionMessage(_In_ HANDLE SectionHandle, 
	_In_ ULONG FileSizeBytes,
	_Out_ NTSTATUS& userReply)
{
	NTSTATUS status = STATUS_CONNECTION_INVALID;
	userReply = STATUS_SUCCESS;

	if (IsConnected())
	{
		auto msg = (PortMessage*)ExAllocatePool2(
			POOL_FLAG_PAGED,
			sizeof(PortMessage) + sizeof(HANDLE),
			COMMUNINCATION_POOLTAG);

		if (msg)
		{
			// POC hack - send the file size in the dataLenBytes buffer
			msg->type = PortMessageType::SectionMessage;
            msg->sectionMsg.fileSizeBytes = FileSizeBytes;
			msg->sectionMsg.sectionHandle = SectionHandle;

			PortReply reply = {};
            ULONG replyLength = sizeof(reply);

			// LARGE_INTEGER timeout;
			// timeout.QuadPart = -10000 * 100; // 100 msec
			status = FltSendMessage(m_Filter,
				&m_ClientPort,
				msg,
				sizeof(PortMessage),
				&reply,
				&replyLength,
				nullptr);

			ExFreePool(msg);

			if (NT_SUCCESS(status))
			{
				userReply = reply.status;
			}
		}
		else
		{
			status = STATUS_NO_MEMORY;
		}
	}

	return status;
}

NTSTATUS CommunicationPort::PortConnectNotify(
	_In_ PFLT_PORT ClientPort,
	_In_opt_ PVOID ServerPortCookie,
	_In_reads_bytes_opt_(SizeOfContext) PVOID ConnectionContext,
	_In_ ULONG SizeOfContext,
	_Outptr_result_maybenull_ PVOID* ConnectionPortCookie)
{
	UNREFERENCED_PARAMETER(ServerPortCookie);
	UNREFERENCED_PARAMETER(ConnectionContext);
	UNREFERENCED_PARAMETER(SizeOfContext);
	
	ConnectionPortCookie = nullptr;

	CommunicationPort* instance = Instance();
	instance->m_ClientPort = ClientPort;
    instance->m_ConnectedProcess = PsGetCurrentProcess();
    instance->m_ConnectedPID = HandleToUlong(PsGetCurrentProcessId());

	return STATUS_SUCCESS;
}

void CommunicationPort::PortDisconnectNotify(_In_opt_ PVOID ConnectionCookie) {
	UNREFERENCED_PARAMETER(ConnectionCookie);

	CommunicationPort* instance = Instance();

	FltCloseClientPort(instance->m_Filter, &instance->m_ClientPort);

	instance->m_ClientPort = {};
	instance->m_ConnectedPID = {};
	instance->m_ConnectedProcess = {};
}

NTSTATUS CommunicationPort::PortMessageNotify(
	_In_opt_ PVOID PortCookie,
	_In_reads_bytes_opt_(InputBufferLength) PVOID InputBuffer,
	_In_ ULONG InputBufferLength,
	_Out_writes_bytes_to_opt_(OutputBufferLength, *ReturnOutputBufferLength) PVOID OutputBuffer,
	_In_ ULONG OutputBufferLength,
	_Out_ PULONG ReturnOutputBufferLength)
{
	UNREFERENCED_PARAMETER(PortCookie);
	UNREFERENCED_PARAMETER(InputBuffer);
	UNREFERENCED_PARAMETER(InputBufferLength);
	UNREFERENCED_PARAMETER(OutputBuffer);
	UNREFERENCED_PARAMETER(OutputBufferLength);

	ReturnOutputBufferLength = 0;

	return STATUS_SUCCESS;
}
#include "pch.h"

#include "Communication.h"
#include "pooltag.h"

static PFLT_PORT s_Port = nullptr;
static PFLT_PORT s_ClientPort = nullptr;
static PFLT_FILTER s_Filter = nullptr;

NTSTATUS InitFilterPort(
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
			&s_Port, 
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

		s_Filter = Filter;

    } while (false);
    
	return status;
}

void FinalizeFilterPort()
{
	if (s_Port)
	{
		FltCloseCommunicationPort(s_Port);
        s_Port = nullptr;
	}
}

// Rudimentary output message sender.
NTSTATUS SendOutputMessage(_In_ PortMessageType type, _In_ LPCWSTR formatString, ...)
{
	NTSTATUS status = STATUS_INTERNAL_ERROR;

	if (s_ClientPort)
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

			UNICODE_STRING tmpString = { 0, COMMUNICATION_BUFFER_LEN - FIELD_OFFSET(PortMessage, data), reinterpret_cast<wchar_t*>(msg->data) };
			if (NT_SUCCESS(status = RtlUnicodeStringVPrintf(&tmpString, formatString, args)))
			{
				msg->dataLenBytes = tmpString.Length;

				// LARGE_INTEGER timeout;
				// timeout.QuadPart = -10000 * 100; // 100 msec
				status = FltSendMessage(s_Filter,
					&s_ClientPort,
					msg,
					msg->dataLenBytes + FIELD_OFFSET(PortMessage, data),
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

NTSTATUS PortConnectNotify(
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
	s_ClientPort = ClientPort;

	return STATUS_SUCCESS;
}

void PortDisconnectNotify(_In_opt_ PVOID ConnectionCookie) {
	UNREFERENCED_PARAMETER(ConnectionCookie);

	FltCloseClientPort(s_Filter, &s_ClientPort);
	s_ClientPort = nullptr;
}

NTSTATUS PortMessageNotify(
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
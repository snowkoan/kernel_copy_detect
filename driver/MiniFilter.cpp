#include "pch.h"
#include "Driver.h"

#include <ktl.h>
#include <Locker.h>

#include "pooltag.h"
#include "Communication.h"
#include "common.h"

// Contexts
#include "VolumeContext.h"
#include "FileContext.h"
#include "StreamHandleContext.h"
#include "StreamContext.h"
#include "DynamicImports.h"
#include "Process.h"

NTSTATUS MinifilterInstanceSetup(PCFLT_RELATED_OBJECTS FltObjects, FLT_INSTANCE_SETUP_FLAGS Flags, DEVICE_TYPE VolumeDeviceType, FLT_FILESYSTEM_TYPE VolumeFilesystemType) 
{
	KdPrint((DRIVER_PREFIX "InstanceSetup FS: %u\n", VolumeFilesystemType));

	UNREFERENCED_PARAMETER(FltObjects);
	UNREFERENCED_PARAMETER(Flags);
	UNREFERENCED_PARAMETER(VolumeDeviceType);
    UNREFERENCED_PARAMETER(VolumeFilesystemType);

	VolumeContext* Context;
    auto status = VolumeContext::Factory(FltObjects, &Context);
	if (!NT_SUCCESS(status))
	{
        KdPrint((DRIVER_PREFIX "Failed to allocate volume context (0x%08X)\n", status));
        return STATUS_FLT_DO_NOT_ATTACH;
	}

	if (!NT_SUCCESS(status = VolumeContext::SetContext(FltObjects, Context, true)))
	{
		KdPrint((DRIVER_PREFIX "Failed to set volume context (0x%08X)\n", status));
	}

	FltReleaseContext(Context);

	return NT_SUCCESS(status) ? STATUS_SUCCESS : STATUS_FLT_DO_NOT_ATTACH;
}

NTSTATUS MinifilterInstanceQueryTeardown(PCFLT_RELATED_OBJECTS FltObjects, FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags) 
{
	UNREFERENCED_PARAMETER(FltObjects);
	UNREFERENCED_PARAMETER(Flags);
	KdPrint((DRIVER_PREFIX "InstanceQueryTeardown\n"));

	return STATUS_SUCCESS;
}

VOID MinifilterInstanceTeardownStart(PCFLT_RELATED_OBJECTS FltObjects, FLT_INSTANCE_TEARDOWN_FLAGS Flags) 
{
	UNREFERENCED_PARAMETER(FltObjects);
	UNREFERENCED_PARAMETER(Flags);
}

VOID MinifilterInstanceTeardownComplete(PCFLT_RELATED_OBJECTS FltObjects, FLT_INSTANCE_TEARDOWN_FLAGS Flags) 
{
	UNREFERENCED_PARAMETER(FltObjects);
	UNREFERENCED_PARAMETER(Flags);
}

FLT_POSTOP_CALLBACK_STATUS OnPostCreate(_Inout_ PFLT_CALLBACK_DATA Data, 
	_In_ PCFLT_RELATED_OBJECTS FltObjects, 
	_In_opt_ PVOID, 
	_In_ FLT_POST_OPERATION_FLAGS Flags) 
{
	const ULONG disposition = (Data->Iopb->Parameters.Create.Options >> 24) & 0xff;
	UNREFERENCED_PARAMETER(disposition); // I always forget how to do this. Keep it around.

	const ULONG desiredAccess = Data->Iopb->Parameters.Create.SecurityContext->DesiredAccess;

	// 
	// Basic things to ignore 
	//
	if (Flags & FLTFL_POST_OPERATION_DRAINING || 
		Data->IoStatus.Status != STATUS_SUCCESS ||
		IoGetTopLevelIrp() != nullptr ||
		Data->RequestorMode == KernelMode)
	{
		return FLT_POSTOP_FINISHED_PROCESSING;
	}

	//
	// Ignore attribute opens to save a *lot* of useless processing.
	//
	if (Data->IoStatus.Information == FILE_OPENED && 
		((desiredAccess & ~(SYNCHRONIZE | FILE_READ_ATTRIBUTES)) == 0))
	{
		return FLT_POSTOP_FINISHED_PROCESSING;
	}

	//
	// Ignore directories
	// 
	BOOLEAN dir = FALSE;
	if (!NT_SUCCESS(FltIsDirectory(FltObjects->FileObject, FltObjects->Instance, &dir)))
	{
		return FLT_POSTOP_FINISHED_PROCESSING;
	}

	if (dir) {
		//
		// not interesting
		//
		return FLT_POSTOP_FINISHED_PROCESSING;
	}

	// We only care about copying in this POC.
	if (!DynamicImports::Instance()->IoCheckFileObjectOpenedAsCopyDestination(FltObjects->FileObject) &&
		!DynamicImports::Instance()->IoCheckFileObjectOpenedAsCopySource(FltObjects->FileObject))
	{
		return FLT_POSTOP_FINISHED_PROCESSING;
	}

	FilterFileNameInformation fileNameInfo(Data);
	if (!fileNameInfo) {
		return FLT_POSTOP_FINISHED_PROCESSING;
	}

	//
	// allocate context
	//
	StreamHandleContext* context;
    auto status = StreamHandleContext::Factory(FltObjects, &context);
	if (!NT_SUCCESS(status)) {
		KdPrint(("Failed to allocate stream handle context (0x%08X)\n", status));
		return FLT_POSTOP_FINISHED_PROCESSING;
	}

	//
	// set stream context
	//
	if (!NT_SUCCESS(status = StreamHandleContext::SetContext(FltObjects, context, true)))
	{
		KdPrint(("Failed to set file context (0x%08X)\n", status));
	}
	else
	{
		UnicodeString processName;
		Process p(PsGetCurrentProcess());
		p.GetImageFileNameOnly(processName);

		SendOutputMessage(PortMessageType::FileMessage,L"%wZ (%u): Created SH context 0x%p for %wZ", 
			processName.Get(), HandleToUlong(PsGetCurrentProcessId()), context, &fileNameInfo->Name);
		if (DynamicImports::Instance()->IoCheckFileObjectOpenedAsCopyDestination(FltObjects->FileObject))
		{
			SendOutputMessage(PortMessageType::FileMessage, L"\tOpened with copy destination flag");
		}
		if (DynamicImports::Instance()->IoCheckFileObjectOpenedAsCopySource(FltObjects->FileObject))
		{
			SendOutputMessage(PortMessageType::FileMessage, L"\tOpened with copy source flag");
		}
	}

	//
	// release context in all cases
	//
	FltReleaseContext(context);

	return FLT_POSTOP_FINISHED_PROCESSING;
}

FLT_PREOP_CALLBACK_STATUS OnPreWrite(
	_Inout_ PFLT_CALLBACK_DATA Data,
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_Outptr_result_maybenull_ PVOID* CompletionContext) 
{
	const auto& Parameters = Data->Iopb->Parameters.Write;
	CompletionContext = nullptr;

	if (IoGetTopLevelIrp() != nullptr)
	{
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	}

	//
	// This may not exist
	//
	StreamHandleContext* context;

	auto status = FltGetStreamHandleContext(FltObjects->Instance,
		FltObjects->FileObject,
		(PFLT_CONTEXT*)&context);

	if (!NT_SUCCESS(status) || context == nullptr) {
		//
		// no context, continue normally
		//
		return FLT_PREOP_SUCCESS_NO_CALLBACK;	
	}

	do {

		FilterFileNameInformation name(Data);
		if (!name)
		{
			break;
		}

		COPY_INFORMATION copyInfo = {};
		status = DynamicImports::Instance()->FltGetCopyInformationFromCallbackData(Data, &copyInfo);
		if (NT_SUCCESS(status))
		{
			// TODO: We should be able to figure out the instance for the source without that much trouble.
			FilterFileNameInformation sourceName (nullptr, copyInfo.SourceFileObject);

			UnicodeString processName;
			Process p(PsGetCurrentProcess());
			p.GetImageFileNameOnly(processName);

			// This is always SYSTEM it seems.
			SendOutputMessage(PortMessageType::FileMessage, L"%wZ (%u): Copy Notification (pos=%u, len=%u)\n\tDestination: %wZ (SH=%p)",
				&processName, HandleToULong(PsGetCurrentProcessId()), Parameters.ByteOffset, Parameters.Length, &name->Name, context);

			if (sourceName)
			{
				SendOutputMessage(PortMessageType::FileMessage, L"\tSource: %wZ", &sourceName.Get()->Name);
			}
			else
			{
				SendOutputMessage(PortMessageType::FileMessage, L"\tSource: %p", copyInfo.SourceFileObject);
			}
		}

		Locker locker(context->Lock);
		context->m_writeCount++;

		if (context->m_writeCount == 1)
		{
			// First write
			KeQuerySystemTimePrecise(&context->m_firstWriteTime);
		}
	} while (false);

	FltReleaseContext(context);

	//
	// don't prevent the write regardless
	//
	return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

FLT_POSTOP_CALLBACK_STATUS OnPostCleanup(_Inout_ PFLT_CALLBACK_DATA Data, 
	_In_ PCFLT_RELATED_OBJECTS FltObjects, 
	_In_opt_ PVOID, 
	_In_ FLT_POST_OPERATION_FLAGS Flags) 
{
	UNREFERENCED_PARAMETER(Flags);
	UNREFERENCED_PARAMETER(Data);

	StreamHandleContext* context;

	auto status = FltGetStreamHandleContext(FltObjects->Instance, FltObjects->FileObject, (PFLT_CONTEXT*)&context);
	if (!NT_SUCCESS(status) || context == nullptr) {
		//
		// no context, continue normally
		//
		return FLT_POSTOP_FINISHED_PROCESSING;
	}

	// SendOutputMessage(PortMessageType::FileMessage, L"Cleaning up stream handle context 0x%p", context);
	FltReleaseContext(context);
	FltDeleteContext(context);

	return FLT_POSTOP_FINISHED_PROCESSING;
}

FLT_PREOP_CALLBACK_STATUS OnPreClose(
	_Inout_ PFLT_CALLBACK_DATA Data,
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_Outptr_result_maybenull_ PVOID* CompletionContext)
{
	UNREFERENCED_PARAMETER(Data);
	UNREFERENCED_PARAMETER(FltObjects);
	*CompletionContext = nullptr;	

	return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

NTSTATUS InitMiniFilter(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) 
{
	HANDLE hKey = nullptr;
	HANDLE hSubKey = nullptr;
	NTSTATUS status;
	do 
	{
		//
		// add registry data for proper mini-filter registration
		//
		OBJECT_ATTRIBUTES keyAttr = RTL_CONSTANT_OBJECT_ATTRIBUTES(RegistryPath, OBJ_KERNEL_HANDLE);
		status = ZwOpenKey(&hKey, KEY_WRITE, &keyAttr);
		if (!NT_SUCCESS(status))
		{
			break;
		}

		UNICODE_STRING subKey = RTL_CONSTANT_STRING(L"Instances");
		OBJECT_ATTRIBUTES subKeyAttr;
		InitializeObjectAttributes(&subKeyAttr, &subKey, OBJ_KERNEL_HANDLE, hKey, nullptr);
		status = ZwCreateKey(&hSubKey, KEY_WRITE, &subKeyAttr, 0, nullptr, 0, nullptr);
		if (!NT_SUCCESS(status))
		{
			break;
		}

		//
		// set "DefaultInstance" value
		//
		UNICODE_STRING valueName = RTL_CONSTANT_STRING(L"DefaultInstance");
		WCHAR name[] = L"BackupDefaultInstance";
		status = ZwSetValueKey(hSubKey, &valueName, 0, REG_SZ, name, sizeof(name));
		if (!NT_SUCCESS(status))
		{
			break;
		}

		//
		// create "instance" key under "Instances"
		//
		UNICODE_STRING instKeyName;
		RtlInitUnicodeString(&instKeyName, name);
		HANDLE hInstKey;
		InitializeObjectAttributes(&subKeyAttr, &instKeyName, OBJ_KERNEL_HANDLE, hSubKey, nullptr);
		status = ZwCreateKey(&hInstKey, KEY_WRITE, &subKeyAttr, 0, nullptr, 0, nullptr);
		if (!NT_SUCCESS(status))
		{
			break;
		}

		//
		// write out altitude 
		// TODO: Don't overwrite this if it exists
		// 
		WCHAR altitude[] = L"335342";
		UNICODE_STRING altitudeName = RTL_CONSTANT_STRING(L"Altitude");
		status = ZwSetValueKey(hInstKey, &altitudeName, 0, REG_SZ, altitude, sizeof(altitude));
		if (!NT_SUCCESS(status))
		{
			break;
		}

		//
		// write out flags
		//
		UNICODE_STRING flagsName = RTL_CONSTANT_STRING(L"Flags");
		ULONG flags = 0;
		status = ZwSetValueKey(hInstKey, &flagsName, 0, REG_DWORD, &flags, sizeof(flags));
		if (!NT_SUCCESS(status))
		{
			break;
		}
	
		ZwClose(hInstKey);
        hInstKey = nullptr;

        ZwClose(hSubKey);
        hSubKey = nullptr;

		FLT_OPERATION_REGISTRATION const callbacks[] = {
			{ IRP_MJ_CREATE, 0, nullptr, OnPostCreate },
			{ IRP_MJ_WRITE, 0, OnPreWrite },
			{ IRP_MJ_CLEANUP, 0, nullptr, OnPostCleanup },
			{ IRP_MJ_CLOSE, 0, OnPreClose, nullptr },
			{ IRP_MJ_OPERATION_END }
		};

		const FLT_CONTEXT_REGISTRATION context[] = {
			{ FLT_FILE_CONTEXT, 0, FileContext::Cleanup, sizeof(FileContext), FILE_POOLTAG },
			{ FLT_VOLUME_CONTEXT, 0, VolumeContext::Cleanup, sizeof(VolumeContext), VOLUME_POOLTAG },
            { FLT_STREAMHANDLE_CONTEXT, 0, StreamHandleContext::Cleanup, sizeof(StreamHandleContext) , STREAMHANDLE_POOLTAG },
            { FLT_STREAM_CONTEXT, 0, StreamContext::Cleanup, sizeof(StreamContext), STREAM_POOLTAG },
			{ FLT_CONTEXT_END }
		};

		FLT_REGISTRATION const reg = {
			sizeof(FLT_REGISTRATION),
			FLT_REGISTRATION_VERSION,
			0,                       //  Flags
			context,                 //  Context
			callbacks,               //  Operation callbacks
			MinifilterUnload,                   //  MiniFilterUnload
			MinifilterInstanceSetup,            //  InstanceSetup
			MinifilterInstanceQueryTeardown,    //  InstanceQueryTeardown
			MinifilterInstanceTeardownStart,    //  InstanceTeardownStart
			MinifilterInstanceTeardownComplete, //  InstanceTeardownComplete
		};
		status = FltRegisterFilter(DriverObject, &reg, &g_Filter);
	} while (false);

	if (hSubKey)
	{
		if (!NT_SUCCESS(status))
		{
			ZwDeleteKey(hSubKey);
		}
		ZwClose(hSubKey);
		hSubKey = nullptr;
	}

	if (hKey)
	{
		ZwClose(hKey);
		hKey = nullptr;
	}

	return status;
}

NTSTATUS MinifilterUnload(FLT_FILTER_UNLOAD_FLAGS Flags)
{
	UNREFERENCED_PARAMETER(Flags);

	FinalizeFilterPort();
	FltUnregisterFilter(g_Filter);

	return STATUS_SUCCESS;
}
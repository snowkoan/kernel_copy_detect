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
#include "SectionContext.h"

#include "DynamicImports.h"
#include "Process.h"
#include "SourceFileList.h"

// We can't directly have a static global, since we don't have global new / delete
SourceFileList* g_SourceFileList;

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

// If FltInstance is NULL, we find it ourselves. Thanks for nothing, buddy.
NTSTATUS SendFileDataToUserMode(PFLT_INSTANCE FltInstance, PFILE_OBJECT FileObject)
{
	NTSTATUS status = STATUS_SUCCESS;

	SectionContext* sectionContext = {};
	VolumeContext* volumeContext = {};
	HANDLE hKernelSection = {};

	HANDLE hTargetProcess = {};
	PEPROCESS pTargetProcessObject = {};

	do
	{
		if (!FltInstance)
		{
			if (!NT_SUCCESS(status = VolumeContext::GetVolumeContextFromFileObject(g_Filter, FileObject, &volumeContext)))
			{
				break;
			}

			FltInstance = volumeContext->fltInstance;
		}

		if (!NT_SUCCESS(status = CommunicationPort::Instance()->GetConnectedProcessHandle(hTargetProcess)) ||
			!NT_SUCCESS(status = CommunicationPort::Instance()->GetConnectedProcessObject(pTargetProcessObject)))
		{
			// If we're not connected don't go any further.
			break;
		}

		status = SectionContext::Factory(g_Filter,
			hTargetProcess, // SectionContext takes ownership of this handle
			pTargetProcessObject, // SectionContext takes ownership of this object
			&sectionContext);

		if (!NT_SUCCESS(status))
		{
			break;
		}
		hTargetProcess = {};
		pTargetProcessObject = {};

		OBJECT_ATTRIBUTES oa = {};
		InitializeObjectAttributes(&oa,
			NULL,
			OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, // We need a kernel handle so verifier doesn't barf when we duplicate.
			NULL,
			NULL);

		LARGE_INTEGER sectionSize = {};
		status = FltCreateSectionForDataScan(FltInstance,
			FileObject,
			sectionContext,
			SECTION_MAP_READ,
			&oa,
			nullptr,
			PAGE_READONLY,
			SEC_COMMIT,
			0,
			&hKernelSection,
			&sectionContext->SectionObject,
			&sectionSize);

		if (!NT_SUCCESS(status))
		{
			break;
		}

		// Bounds checking is a good idea...
		if (sectionSize.QuadPart == 0 ||
			sectionSize.QuadPart > (static_cast<ULONGLONG>(1024) * 1024 * 32)) // 32MB
		{
			break;
		}

		sectionContext->SectionSize = static_cast<ULONG>(sectionSize.QuadPart);

		// Now duplicate the handle into our user mode process
		if (!NT_SUCCESS(status = ZwDuplicateObject(ZwCurrentProcess(),
			hKernelSection,
			sectionContext->TargetProcessHandle,
			&sectionContext->SectionHandle,
			0,
			0,
			DUPLICATE_SAME_ACCESS)))
		{
			break;
		}

		if (NT_SUCCESS(status = CommunicationPort::Instance()->SendSectionMessage(sectionContext->SectionHandle,
			sectionContext->SectionSize)))
		{
			// User mode is responsible for closing the section handle now.
            sectionContext->SectionHandle = nullptr;
		}

	} while (false);

	if (volumeContext)
	{
		FltReleaseContext(volumeContext);
	}

	if (sectionContext)
	{
		if (sectionContext->IsSectionCreated())
		{
			FltCloseSectionForDataScan(sectionContext);
		}
		FltReleaseContext(sectionContext);
		sectionContext = nullptr;
	}

	if (hTargetProcess)
	{
		ZwClose(hTargetProcess);
		hTargetProcess = nullptr;
	}

	if (hKernelSection)
	{
		ZwClose(hKernelSection);
		hKernelSection = nullptr;
	}

	if (pTargetProcessObject)
	{
        ObDereferenceObject(pTargetProcessObject);
        pTargetProcessObject = nullptr;
	}

	return status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
FLT_POSTOP_CALLBACK_STATUS OnPostCreate(_Inout_ PFLT_CALLBACK_DATA Data, 
	_In_ PCFLT_RELATED_OBJECTS FltObjects, 
	_In_opt_ PVOID, 
	_In_ FLT_POST_OPERATION_FLAGS Flags) 
{
	const ULONG disposition = (Data->Iopb->Parameters.Create.Options >> 24) & 0xff;
	UNREFERENCED_PARAMETER(disposition); // I always forget how to do this. Keep it around.

	const ULONG desiredAccess = Data->Iopb->Parameters.Create.SecurityContext->DesiredAccess;
	PFILE_OBJECT PotentialSourceFileObject = {};

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

	if (HandleToUlong(PsGetCurrentProcessId()) == CommunicationPort::Instance()->GetConnectedPID())
    {
        // Don't care about our own activity
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
	if (!NT_SUCCESS(FltIsDirectory(FltObjects->FileObject, FltObjects->Instance, &dir)) || dir)
	{
		return FLT_POSTOP_FINISHED_PROCESSING;
	}

	//
	// We only care about copying in this POC.
	//
	if (DynamicImports::Instance()->IoCheckFileObjectOpenedAsCopyDestination(FltObjects->FileObject))
	{
		if (0 == (desiredAccess & FILE_WRITE_DATA))
		{
			return FLT_POSTOP_FINISHED_PROCESSING;
		}

		// The FO is being opened as a copy destination. This may or may not prove to be true.
		// We don't know the source, but by observation, CopyFile opens it first so we should have it 
		// in our list.
        PotentialSourceFileObject = g_SourceFileList->Find(HandleToUlong(PsGetCurrentProcessId()),
            HandleToUlong(PsGetCurrentThreadId()));
	}
	else if (DynamicImports::Instance()->IoCheckFileObjectOpenedAsCopySource(FltObjects->FileObject))
	{
		if (0 == (desiredAccess & FILE_READ_DATA))
		{
			return FLT_POSTOP_FINISHED_PROCESSING;
		}

		// This FO is being opened as a copy source. This may or may not prove to be true.
		// By observation, Explorer will sometimes open with this flag set and then close the file before copying anything.
        g_SourceFileList->AddFirst(HandleToUlong(PsGetCurrentProcessId()), 
								   HandleToUlong(PsGetCurrentThreadId()), 
								   FltObjects->FileObject);
	}
	else
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
		KdPrint(("Failed to set SH context (0x%08X)\n", status));
	}
	else
	{
		UnicodeString processName;
		Process p(PsGetCurrentProcess());
		p.GetImageFileNameOnly(processName);

		CommunicationPort::Instance()->SendOutputMessage(PortMessageType::FileMessage,
			L"%wZ (%u,%u): Created context SH=%p, FO=%p, %wZ%ls%ls", 
			processName.Get(), HandleToUlong(PsGetCurrentProcessId()), HandleToUlong(PsGetCurrentThreadId()),
			context, 
            FltObjects->FileObject,
			&fileNameInfo->Name,
			DynamicImports::Instance()->IoCheckFileObjectOpenedAsCopyDestination(FltObjects->FileObject) ? L"\n\tOpened with copy destination flag" : L"",
			DynamicImports::Instance()->IoCheckFileObjectOpenedAsCopySource(FltObjects->FileObject) ? L"\n\tOpened with copy source flag" : L"");
	}

	if (PotentialSourceFileObject)
	{
		// We think we know the source that will be copied to the current FO. Let user mode know.
		// We don't know the correct instance for this FO. It's OK.
		status = SendFileDataToUserMode(nullptr, PotentialSourceFileObject);
	}

	//
	// release context in all cases
	//
	FltReleaseContext(context);

	return FLT_POSTOP_FINISHED_PROCESSING;
}

_IRQL_requires_max_(APC_LEVEL)
FLT_PREOP_CALLBACK_STATUS OnPreWrite(
	_Inout_ PFLT_CALLBACK_DATA Data,
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_Outptr_result_maybenull_ PVOID* CompletionContext) 
{
	const auto& Parameters = Data->Iopb->Parameters.Write;
	CompletionContext = nullptr;

	StreamHandleContext* contextSrc = {};
	StreamHandleContext* contextDest = {};
	VolumeContext* volumeContext = {};

	if (IoGetTopLevelIrp() != nullptr)
	{
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	}

	do {

		//
		// This may not exist
		//
		auto status = FltGetStreamHandleContext(FltObjects->Instance,
			FltObjects->FileObject,
			(PFLT_CONTEXT*)&contextSrc);

		if (!NT_SUCCESS(status) || contextSrc == nullptr) {
			//
			// no context, that's normal.
			//
			break;
		}

		// We only care if this is NtCopyFileChunk
		// TODO: We could also check for previous mode (it should be kernel)
		COPY_INFORMATION copyInfo = {};
		status = DynamicImports::Instance()->FltGetCopyInformationFromCallbackData(Data, &copyInfo);
		if (NT_SUCCESS(status))
		{
			status = VolumeContext::GetVolumeContextFromFileObject(FltObjects->Filter, copyInfo.SourceFileObject, &volumeContext);
			if (!NT_SUCCESS(status))
			{
				// That's weird... we should have a context for this file.
				break;
			}

			FilterFileNameInformation destinationName(Data);
			if (!destinationName)
			{
				// Also weird.
				break;
			}

			FilterFileNameInformation sourceName(volumeContext->fltInstance, copyInfo.SourceFileObject);

			UnicodeString processName;
			Process p(PsGetCurrentProcess());
			p.GetImageFileNameOnly(processName);

			// This is always called in the SYSTEM process it seems.
			CommunicationPort::Instance()->SendOutputMessage(PortMessageType::FileMessage,
				L"%wZ (%u, %u): Copy Notification (pos=%u, len=%u)\n\tDestination: %wZ (SH=%p)\n\tSource: %wZ",
				&processName,
				HandleToULong(PsGetCurrentProcessId()),
				HandleToULong(PsGetCurrentThreadId()),
				Parameters.ByteOffset, Parameters.Length,
				&destinationName->Name,
				contextSrc,
				sourceName ? &sourceName.Get()->Name : Process::GetUnknownProcessName());

			Locker locker(contextSrc->Lock);
			contextSrc->m_writeCount++;
		}

	} while (false);

	if (contextSrc)
	{
		FltReleaseContext(contextSrc);
		contextSrc = nullptr;
	}
	if (contextDest)
	{
		FltReleaseContext(contextDest);
		contextDest = nullptr;
	}
	if (volumeContext)
	{
		FltReleaseContext(volumeContext);
		volumeContext = nullptr;
	}

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

	if (DynamicImports::Instance()->IoCheckFileObjectOpenedAsCopySource(FltObjects->FileObject))
	{
        g_SourceFileList->Remove(FltObjects->FileObject);
	}

	auto status = FltGetStreamHandleContext(FltObjects->Instance, FltObjects->FileObject, (PFLT_CONTEXT*)&context);
	if (!NT_SUCCESS(status) || context == nullptr) {
		//
		// no context, continue normally
		//
		return FLT_POSTOP_FINISHED_PROCESSING;
	}

	CommunicationPort::Instance()->SendOutputMessage(PortMessageType::FileMessage, 
		L"Cleaning up stream handle context 0x%p", context);

	FltReleaseContext(context);
	FltDeleteContext(context);

	return FLT_POSTOP_FINISHED_PROCESSING;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
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
		// Set up our list
		// 
		g_SourceFileList = new SourceFileList();

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
            { FLT_SECTION_CONTEXT, 0, SectionContext::Cleanup, sizeof(SectionContext), SECTION_POOLTAG },
			{ FLT_CONTEXT_END }
		};

		FLT_REGISTRATION const reg = {
			sizeof(FLT_REGISTRATION),
			FLT_REGISTRATION_VERSION,
			FLTFL_OPERATION_REGISTRATION_SKIP_PAGING_IO, //  Flags
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


	delete g_SourceFileList;
	CommunicationPort::Instance()->FinalizeFilterPort();
	FltUnregisterFilter(g_Filter);

	return STATUS_SUCCESS;
}


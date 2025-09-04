#pragma once

class VolumeContext {

public:

	UNICODE_STRING volumeName = {};
    PFLT_VOLUME_PROPERTIES volumeProperties = {};
	PFLT_INSTANCE fltInstance = {}; // We only ever attach once. What could go wrong?

	VolumeContext()
	{

	}

    static NTSTATUS Factory(_In_ PCFLT_RELATED_OBJECTS FltObjects, _Out_ VolumeContext** Context)
    {
        *Context = nullptr;

        auto status = FltAllocateContext(FltObjects->Filter,
            FLT_VOLUME_CONTEXT, 
			sizeof(VolumeContext), 
			PagedPool,
            (PFLT_CONTEXT*)Context);

        if (!NT_SUCCESS(status)) {
            return status;
        }

        // Placement new
        new (*Context) VolumeContext();

		// Do this outside of constructor so that we can return status
        if (!NT_SUCCESS(status = (*Context)->GetVolumeName(FltObjects->Volume, (*Context)->volumeName))) {
            FltReleaseContext(*Context);
            *Context = nullptr;
            return status;
        }

        if (!NT_SUCCESS(status = (*Context)->GetVolumeProperties(FltObjects->Volume, &(*Context)->volumeProperties))) {
            FltReleaseContext(*Context);
            *Context = nullptr;
            return status;
        }

		(*Context)->fltInstance = FltObjects->Instance;
        
		return STATUS_SUCCESS;
    }

	static NTSTATUS SetContext(PCFLT_RELATED_OBJECTS FltObjects, VolumeContext* Context, bool Replace)
	{
		VolumeContext* OldContext = nullptr;

		auto status = FltSetVolumeContext(FltObjects->Volume,
			Replace ? FLT_SET_CONTEXT_REPLACE_IF_EXISTS : FLT_SET_CONTEXT_KEEP_IF_EXISTS,
			Context,
			reinterpret_cast<PFLT_CONTEXT*>(&OldContext));

		if (OldContext)
		{
			FltReleaseContext(OldContext);
		}

		return status;
	}

	// This is essentially our destructor, called by Filter Manager when the context is deleted
    static void Cleanup(_In_ PFLT_CONTEXT Context, _In_ FLT_CONTEXT_TYPE ContextType)
    {
        if (ContextType != FLT_VOLUME_CONTEXT)
        {
            ASSERT(FALSE);
            return;
        }

        auto ctx = reinterpret_cast<VolumeContext*>(Context);
        if (ctx->volumeName.Buffer)
        {
            ExFreePoolWithTag(ctx->volumeName.Buffer, VOLUME_POOLTAG);
            ctx->volumeName.Buffer = nullptr;
        }

		if (ctx->volumeProperties)
		{
			ExFreePoolWithTag(ctx->volumeProperties, VOLUME_POOLTAG);
			ctx->volumeProperties = nullptr;
		}

        return;
    }

	static NTSTATUS GetVolumeContextFromFileObject(PFLT_FILTER Filter, PFILE_OBJECT FileObject, _Out_ VolumeContext** Context)
	{
		*Context = nullptr;
		PFLT_VOLUME Volume = {};
		NTSTATUS status = FltGetVolumeFromFileObject(Filter, FileObject, &Volume);
        if (NT_SUCCESS(status)) {
			status = FltGetVolumeContext(Filter, Volume, reinterpret_cast<PFLT_CONTEXT*>(Context));
			FltObjectDereference(Volume);
        }

		return status;
	}

private:

	NTSTATUS GetVolumeName(_In_ PFLT_VOLUME Volume, _Out_ UNICODE_STRING& VolumeName)
	{
		auto status = STATUS_SUCCESS;
		VolumeName = { 0 };

		// Get the volume name
		do
		{
			// Step 1: First call to FltGetVolumeName to determine the required buffer size.
			// We expect STATUS_BUFFER_TOO_SMALL, and returnedLength will be updated.
			ULONG returnedLength = 0;
			status = FltGetVolumeName(
				Volume,
				NULL, // No buffer provided
				&returnedLength
			);

			// We expect STATUS_BUFFER_TOO_SMALL here. If it's another error, get out.
			if (status != STATUS_BUFFER_TOO_SMALL) {
				CommunicationPort::Instance()->SendOutputMessage(PortMessageType::VolumeMessage, 
					L"FltGetVolumeName (first call) failed with unexpected status: 0x%X\n", status);
				break;
			}

			volumeName.Buffer = (wchar_t*)ExAllocatePool2(POOL_FLAG_NON_PAGED, returnedLength, VOLUME_POOLTAG);
			if (!volumeName.Buffer) {
				break;
			}
			volumeName.MaximumLength = static_cast<USHORT>(returnedLength);

			// Step 2. Get the name
			status = FltGetVolumeName(Volume,
				&volumeName,
				nullptr);

			if (!NT_SUCCESS(status)) {
				CommunicationPort::Instance()->SendOutputMessage(PortMessageType::VolumeMessage, 
					L"FltGetVolumeName failed with status: 0x%X\n", status);
				break;
			}

		} while (false);

		if (!NT_SUCCESS(status)) {
			if (volumeName.Buffer) {
				ExFreePoolWithTag(volumeName.Buffer, VOLUME_POOLTAG);
				volumeName.Buffer = nullptr;
			}
		}

		return status;
	}

	NTSTATUS GetVolumeProperties(_In_ PFLT_VOLUME Volume, _Out_ PFLT_VOLUME_PROPERTIES* Properties)
	{
		*Properties = nullptr;

        ULONG returnedLength = 0;
        auto status = FltGetVolumeProperties(Volume, 
			nullptr,
			0,
			&returnedLength);

        if (status != STATUS_BUFFER_TOO_SMALL) {
            return status;
        }

		*Properties = (PFLT_VOLUME_PROPERTIES)ExAllocatePool2(POOL_FLAG_NON_PAGED, returnedLength, VOLUME_POOLTAG);

		if (nullptr == *Properties)
		{
			return STATUS_INSUFFICIENT_RESOURCES;
		}

        status = FltGetVolumeProperties(Volume,
            *Properties,
            returnedLength,
            &returnedLength);       

		if (!NT_SUCCESS(status))
		{
			ExFreePoolWithTag(*Properties, VOLUME_POOLTAG);
			*Properties = nullptr;
		}

		return status;
	}
};
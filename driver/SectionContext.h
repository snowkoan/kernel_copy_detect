#pragma once

class SectionContext {

public:
    HANDLE SectionHandle = {};
    PVOID SectionObject = {};
    ULONG SectionSize = {};

    HANDLE TargetProcessHandle = {};

    // We take ownership of the handle, to prevent races.
    SectionContext(HANDLE ProcessHandle)
    {
        TargetProcessHandle = ProcessHandle;
    }

    ~SectionContext()
    {
        if (SectionHandle)
        {
            ZwClose(SectionHandle);
            SectionHandle = nullptr;
        }

        if (SectionObject)
        {
            ObDereferenceObject(SectionObject);
            SectionObject = nullptr;
        }

        if (TargetProcessHandle && TargetProcessHandle != ZwCurrentProcess())
        {
            ZwClose(TargetProcessHandle);
            TargetProcessHandle = nullptr;
        }
    }

    bool IsSectionCreated() const
    {
        return SectionHandle != nullptr;
    }

    static NTSTATUS Factory(_In_ PCFLT_RELATED_OBJECTS FltObjects, 
        _In_ HANDLE ProcessHandle,
        _Out_ SectionContext** Context)
    {
        *Context = nullptr;

        auto status = FltAllocateContext(FltObjects->Filter,
            FLT_SECTION_CONTEXT, sizeof(SectionContext), NonPagedPool,
            (PFLT_CONTEXT*)Context);

        if (!NT_SUCCESS(status)) {
            return status;
        }

        // Placement new
        new (*Context) SectionContext(ProcessHandle);

        return STATUS_SUCCESS;
    }

    // This is essentially our destructor, called by Filter Manager when the context is deleted
    static void Cleanup(_In_ PFLT_CONTEXT Context, _In_ FLT_CONTEXT_TYPE ContextType)
    {
        if (ContextType != FLT_SECTION_CONTEXT)
        {
            ASSERT(FALSE);
            return;
        }
        auto ctx = reinterpret_cast<SectionContext*>(Context);
        ctx->~SectionContext();

        return;
    }
};
#pragma once

class SectionContext {

public:
    HANDLE SectionHandle = {};
    PVOID SectionObject = {};
    ULONG SectionSize = {};

    HANDLE TargetProcessHandle = {};
    PEPROCESS TargetProcessObject = {}; // For closing the section handle later on

    // We take ownership of the handle, to prevent races.
    SectionContext(HANDLE ProcessHandle, PEPROCESS ProcessObject)
    {
        TargetProcessHandle = ProcessHandle;
        TargetProcessObject = ProcessObject;
    }

    ~SectionContext()
    {
        if (SectionHandle)
        {
            // SectionHandle is in our user mode process handle table.
            KAPC_STATE apcState;
            KeStackAttachProcess(TargetProcessObject, &apcState);
            // ZwClose does not like user mode handles in kernel. Verifier barfs.
            ObCloseHandle(SectionHandle, UserMode);
            KeUnstackDetachProcess(&apcState);
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

        if (TargetProcessObject)
        {
            ObfDereferenceObject(TargetProcessObject);
        }
    }

    bool IsSectionCreated() const
    {
        return SectionObject != nullptr;
    }

    static NTSTATUS Factory(
        _In_ PFLT_FILTER Filter,
        _In_ HANDLE ProcessHandle,
        _In_ PEPROCESS ProcessObject,
        _Out_ SectionContext** Context)
    {
        *Context = nullptr;

        auto status = FltAllocateContext(Filter,
            FLT_SECTION_CONTEXT, sizeof(SectionContext), NonPagedPool,
            (PFLT_CONTEXT*)Context);

        if (!NT_SUCCESS(status)) {
            return status;
        }

        // Placement new
        new (*Context) SectionContext(ProcessHandle, ProcessObject);

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
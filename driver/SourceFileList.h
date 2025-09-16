#pragma once

#include "LinkedList.h"

struct SourceFileEntry
{
    LIST_ENTRY   Link; // For LinkedList
    ULONG        ProcessId;
    ULONG        ThreadId;
    PFILE_OBJECT FileObject;
    NTSTATUS     Verdict;
};

class SourceFileList 
{

public:     
    SourceFileList()
    {
        m_List.Init();
        m_Lock.Init();
    }
    ~SourceFileList()
    {
        while (!m_List.IsEmpty())
        {
            auto entry = m_List.RemoveHead();
            ExFreePoolWithTag(entry, SOURCEFILE_POOLTAG);
        }
        m_List.Finalize();
        m_Lock.Delete();
    }

    bool AddFirst(ULONG processId, ULONG threadId, PFILE_OBJECT fileObject)
    {
        Locker locker(m_Lock);

        auto entry = (SourceFileEntry*)ExAllocatePool2(POOL_FLAG_PAGED, sizeof(SourceFileEntry), SOURCEFILE_POOLTAG);
        if (!entry)
        {
            return false;
        }

        entry->ProcessId = processId;
        entry->ThreadId = threadId;
        entry->FileObject = fileObject; // Don't add a ref here. We do it in find, so that we can still see CLOSE.
        entry->Verdict = STATUS_PENDING; // No verdict yet

        m_List.AddHead(entry);

        return true;
    }

    bool Remove(PFILE_OBJECT fileObject)
    {
        Locker locker(m_Lock);

        SourceFileEntry* entry = m_List.Find([fileObject](SourceFileEntry* e) 
            { return e->FileObject == fileObject; });

        if (entry)
        {
            m_List.RemoveItem(entry);
            // ObDereferenceObject(fileObject); // We no longer addref this when adding.
            ExFreePoolWithTag(entry, SOURCEFILE_POOLTAG);
            return true;
        }

        return false;
    }

    NTSTATUS UpdateVerdict(_In_ ULONG threadId, _In_ PFILE_OBJECT FileObject, _In_ NTSTATUS NewVerdict)
    {
        // We're doing an atomic operation here - we can use a shared locker.
        SharedLocker locker(m_Lock);

        SourceFileEntry* entry = m_List.Find([threadId](SourceFileEntry* e)
            { return e->ThreadId == threadId; });

        if (entry)
        {

            ASSERT(FileObject == entry->FileObject);

            InterlockedExchange((volatile LONG*)&entry->Verdict, NewVerdict);

            return STATUS_SUCCESS;
        }

        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    // Caller is responsible for calling ObDereferenceObject on the returned FO
    NTSTATUS Find(ULONG threadId, _Out_ PFILE_OBJECT& FileObject, _Out_ NTSTATUS& Verdict)
    {
        FileObject = nullptr;
        Verdict = STATUS_PENDING; // no verdict

        SharedLocker locker(m_Lock);

        SourceFileEntry* entry = m_List.Find([threadId](SourceFileEntry* e)
            { return e->ThreadId == threadId; });

        if (entry)
        {
            // Sanity check - make sure we don't have duplicate open source files for this pid/tid. It's OK
            // if we have multiple FOs for the same file (it's not clear that this happens in practice but...)
            // 
            // We use a file system trick for our sanity -- all FOs that point to the same stream
            // have the same FsContext pointer. This is not quite true for network, but true enough
            // for our purposes.
            m_List.ForEach([threadId, entry](SourceFileEntry* e)
            {
                if (e->ThreadId == threadId)
                {
                    if (entry->FileObject->FsContext != e->FileObject->FsContext)
                    {
                        CommunicationPort::Instance()->SendOutputMessage(PortMessageType::FileMessage,
                            L"Warning: Multiple source files found for PID %u TID %u - FsContext mismatch (0x%p vs 0x%p)",
                            entry->ProcessId,
                            threadId,
                            entry->FileObject->FsContext,
                            e->FileObject->FsContext);
                    }
                }
            });

            // Let's be careful about lifetimes...
            ObReferenceObject(entry->FileObject);
            FileObject = entry->FileObject;
            Verdict = entry->Verdict;
            return STATUS_SUCCESS;
        }

        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    void* operator new (size_t s)
    {
        // Need nonpaged for synch object - verifier complains
        return ExAllocatePool2(POOL_FLAG_NON_PAGED, s, SOURCEFILE_POOLTAG);
    }

    void operator delete(void* p)
    {
        if (p)
        {
            ExFreePoolWithTag(p, SOURCEFILE_POOLTAG);
        }
    }

private:

    LinkedList<SourceFileEntry> m_List;
    EResource m_Lock;
};

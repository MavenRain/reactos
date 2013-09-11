#include "npfs.h"

LONG NpSlowWriteCalls;

BOOLEAN
NTAPI
NpCommonWrite(IN PFILE_OBJECT FileObject,
              IN PVOID Buffer,
              IN ULONG DataSize, 
              IN PETHREAD Thread, 
              IN PIO_STATUS_BLOCK IoStatus, 
              IN PIRP Irp, 
              IN PLIST_ENTRY List)
{
    NODE_TYPE_CODE NodeType;
    BOOLEAN WriteOk;
    PNP_CCB Ccb;
    PNP_NONPAGED_CCB NonPagedCcb;
    PNP_DATA_QUEUE WriteQueue;
    NTSTATUS Status;
    PNP_EVENT_BUFFER EventBuffer;
    ULONG BytesWritten, NamedPipeEnd;
    PAGED_CODE();

    IoStatus->Information = 0;
    NodeType = NpDecodeFileObject(FileObject, NULL, &Ccb, &NamedPipeEnd);

    if (!NodeType)
    {
        IoStatus->Status = STATUS_PIPE_DISCONNECTED;
        return TRUE;
    }

    if (NodeType != NPFS_NTC_CCB)
    {
        IoStatus->Status = STATUS_INVALID_PARAMETER;
        return TRUE;
    }

    NonPagedCcb = Ccb->NonPagedCcb;
    ExAcquireResourceExclusiveLite(&NonPagedCcb->Lock, TRUE);

    if (Ccb->NamedPipeState != FILE_PIPE_CONNECTED_STATE)
    {
        if (Ccb->NamedPipeState == FILE_PIPE_DISCONNECTED_STATE)
        {
            IoStatus->Status = STATUS_PIPE_DISCONNECTED;
        }
        else if (Ccb->NamedPipeState == FILE_PIPE_LISTENING_STATE)
        {
            IoStatus->Status = STATUS_PIPE_LISTENING;
        }
        else
        {
            ASSERT(Ccb->NamedPipeState == FILE_PIPE_CLOSING_STATE);
            IoStatus->Status = STATUS_PIPE_CLOSING;
        }

        WriteOk = TRUE;
        goto Quickie;
    }

    if ((NamedPipeEnd == FILE_PIPE_SERVER_END && Ccb->Fcb->NamedPipeConfiguration == FILE_PIPE_INBOUND) ||
        (NamedPipeEnd == FILE_PIPE_CLIENT_END && Ccb->Fcb->NamedPipeConfiguration == FILE_PIPE_OUTBOUND))
    {
        IoStatus->Status = STATUS_INVALID_PARAMETER;
        WriteOk = TRUE;
        goto Quickie;
    }

    IoStatus->Status = STATUS_SUCCESS;
    IoStatus->Information = DataSize;

    if (NamedPipeEnd == FILE_PIPE_SERVER_END)
    {
        WriteQueue = &Ccb->DataQueue[FILE_PIPE_OUTBOUND];
    }
    else
    {
        WriteQueue = &Ccb->DataQueue[FILE_PIPE_INBOUND];
    }

    EventBuffer = NonPagedCcb->EventBuffer[NamedPipeEnd];

    if ((WriteQueue->QueueState == ReadEntries &&
        WriteQueue->BytesInQueue < DataSize &&
        WriteQueue->Quota < DataSize - WriteQueue->BytesInQueue) ||
        (WriteQueue->QueueState == ReadEntries &&
        WriteQueue->Quota - WriteQueue->QuotaUsed < DataSize))
    {
        if (Ccb->Fcb->NamedPipeType == FILE_PIPE_MESSAGE_TYPE &&
            Ccb->CompletionMode[NamedPipeEnd] == FILE_PIPE_COMPLETE_OPERATION)
        {
            IoStatus->Information = 0;
            IoStatus->Status = STATUS_SUCCESS;
            WriteOk = TRUE;
            goto Quickie;
        }

        if (!Irp)
        {
            WriteOk = FALSE;
            goto Quickie;
        }
    }

    Status = NpWriteDataQueue(WriteQueue,
                              Ccb->ReadMode[NamedPipeEnd],
                              Buffer,
                              DataSize,
                              Ccb->Fcb->NamedPipeType,
                              &BytesWritten,
                              Ccb,
                              NamedPipeEnd,
                              Thread,
                              List);
    IoStatus->Status = Status;

    if (Status == STATUS_MORE_PROCESSING_REQUIRED)
    {
        ASSERT(WriteQueue->QueueState != ReadEntries);
        if ((Ccb->CompletionMode[NamedPipeEnd] == FILE_PIPE_COMPLETE_OPERATION || !Irp)
            && WriteQueue->Quota - WriteQueue->QuotaUsed < BytesWritten)
        {
            IoStatus->Information = DataSize - BytesWritten;
            IoStatus->Status = STATUS_SUCCESS;
        }
        else
        {
            ASSERT(WriteQueue->QueueState != ReadEntries);

            IoStatus->Status = NpAddDataQueueEntry(NamedPipeEnd,
                                                   Ccb,
                                                   WriteQueue,
                                                   WriteEntries,
                                                   Buffered,
                                                   DataSize,
                                                   Irp,
                                                   Buffer,
                                                   DataSize - BytesWritten);
        }
    }

    if (EventBuffer) KeSetEvent(EventBuffer->Event, IO_NO_INCREMENT, FALSE);
    WriteOk = TRUE;

Quickie:
    ExReleaseResourceLite(&Ccb->NonPagedCcb->Lock);
    return WriteOk;
}

NTSTATUS
NTAPI
NpFsdWrite(IN PDEVICE_OBJECT DeviceObject,
           IN PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    IO_STATUS_BLOCK IoStatus;
    LIST_ENTRY List;
    PLIST_ENTRY NextEntry, ThisEntry;
    PIRP ListIrp;
    PAGED_CODE();
    NpSlowWriteCalls++;

    InitializeListHead(&List);
    IoStack = IoGetCurrentIrpStackLocation(Irp);

    FsRtlEnterFileSystem();
    ExAcquireResourceSharedLite(&NpVcb->Lock, TRUE);

    NpCommonWrite(IoStack->FileObject,
                  Irp->UserBuffer,
                  IoStack->Parameters.Write.Length,
                  Irp->Tail.Overlay.Thread,
                  &IoStatus,
                  Irp,
                  &List);

    ExReleaseResourceLite(&NpVcb->Lock);

    NextEntry = List.Flink;
    while (NextEntry != &List)
    {
        ThisEntry = NextEntry;
        NextEntry = NextEntry->Flink;

        ListIrp = CONTAINING_RECORD(ThisEntry, IRP, Tail.Overlay.ListEntry);
        IoCompleteRequest(ListIrp, IO_NAMED_PIPE_INCREMENT);
    }

    FsRtlExitFileSystem();

    if (IoStatus.Status != STATUS_PENDING)
    {
        Irp->IoStatus.Information = IoStatus.Information;
        Irp->IoStatus.Status = IoStatus.Status;
        IoCompleteRequest(Irp, IO_NAMED_PIPE_INCREMENT);
    }

    return IoStatus.Status;
}

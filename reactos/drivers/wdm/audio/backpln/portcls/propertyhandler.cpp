/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Kernel Streaming
 * FILE:            drivers/wdm/audio/backpln/portcls/propertyhandler.cpp
 * PURPOSE:         Pin property handler
 * PROGRAMMER:      Johannes Anderwald
 */

#include "private.hpp"

NTSTATUS
FindPropertyHandler(
    IN PIO_STATUS_BLOCK IoStatus,
    IN PSUBDEVICE_DESCRIPTOR Descriptor,
    IN PKSPROPERTY Property,
    IN ULONG InputBufferLength,
    IN ULONG OutputBufferLength,
    OUT PVOID OutputBuffer,
    OUT PFNKSHANDLER *PropertyHandler);

NTSTATUS
HandlePropertyInstances(
    IN PIO_STATUS_BLOCK IoStatus,
    IN PKSIDENTIFIER  Request,
    IN OUT PVOID  Data,
    IN PSUBDEVICE_DESCRIPTOR Descriptor,
    IN BOOL Global)
{
    KSPIN_CINSTANCES * Instances;
    KSP_PIN * Pin = (KSP_PIN*)Request;

    if (Pin->PinId >= Descriptor->Factory.PinDescriptorCount)
    {
        IoStatus->Information = 0;
        IoStatus->Status = STATUS_INVALID_PARAMETER;
        return STATUS_INVALID_PARAMETER;
    }

    Instances = (KSPIN_CINSTANCES*)Data;

    if (Global)
        Instances->PossibleCount = Descriptor->Factory.Instances[Pin->PinId].MaxGlobalInstanceCount;
    else
        Instances->PossibleCount = Descriptor->Factory.Instances[Pin->PinId].MaxFilterInstanceCount;

    Instances->CurrentCount = Descriptor->Factory.Instances[Pin->PinId].CurrentPinInstanceCount;

    IoStatus->Information = sizeof(KSPIN_CINSTANCES);
    IoStatus->Status = STATUS_SUCCESS;
    return STATUS_SUCCESS;
}

NTSTATUS
HandleNecessaryPropertyInstances(
    IN PIO_STATUS_BLOCK IoStatus,
    IN PKSIDENTIFIER  Request,
    IN OUT PVOID  Data,
    IN PSUBDEVICE_DESCRIPTOR Descriptor)
{
    PULONG Result;
    KSP_PIN * Pin = (KSP_PIN*)Request;

    if (Pin->PinId >= Descriptor->Factory.PinDescriptorCount)
    {
        IoStatus->Information = 0;
        IoStatus->Status = STATUS_INVALID_PARAMETER;
        return STATUS_INVALID_PARAMETER;
    }

    Result = (PULONG)Data;
    *Result = Descriptor->Factory.Instances[Pin->PinId].MinFilterInstanceCount;

    IoStatus->Information = sizeof(ULONG);
    IoStatus->Status = STATUS_SUCCESS;
    return STATUS_SUCCESS;
}

NTSTATUS
HandleDataIntersection(
    IN PIO_STATUS_BLOCK IoStatus,
    IN PKSIDENTIFIER Request,
    IN OUT PVOID  Data,
    IN ULONG DataLength,
    IN PSUBDEVICE_DESCRIPTOR Descriptor,
    IN ISubdevice *SubDevice)
{
    KSP_PIN * Pin = (KSP_PIN*)Request;
    PKSMULTIPLE_ITEM MultipleItem;
    PKSDATARANGE DataRange;
    NTSTATUS Status = STATUS_NO_MATCH;
    ULONG Index, Length;

    // Access parameters
    MultipleItem = (PKSMULTIPLE_ITEM)(Pin + 1);
    DataRange = (PKSDATARANGE)(MultipleItem + 1);

    for(Index = 0; Index < MultipleItem->Count; Index++)
    {
        // Call miniport's properitary handler
        PC_ASSERT(Descriptor->Factory.KsPinDescriptor[Pin->PinId].DataRangesCount);
        PC_ASSERT(Descriptor->Factory.KsPinDescriptor[Pin->PinId].DataRanges[0]);
        Status = SubDevice->DataRangeIntersection(Pin->PinId, DataRange, (PKSDATARANGE)Descriptor->Factory.KsPinDescriptor[Pin->PinId].DataRanges[0],
                                                  DataLength, Data, &Length);

        if (Status == STATUS_SUCCESS)
        {
            IoStatus->Information = Length;
            break;
        }
        DataRange =  (PKSDATARANGE)	UlongToPtr(PtrToUlong(DataRange) + DataRange->FormatSize);
    }

    IoStatus->Status = Status;
    return Status;
}

NTSTATUS
HandlePhysicalConnection(
    IN PIO_STATUS_BLOCK IoStatus,
    IN PKSIDENTIFIER Request,
    IN ULONG RequestLength,
    IN OUT PVOID  Data,
    IN ULONG DataLength,
    IN PSUBDEVICE_DESCRIPTOR Descriptor)
{
    PKSP_PIN Pin;
    PLIST_ENTRY Entry;
    PKSPIN_PHYSICALCONNECTION Connection;
    PPHYSICAL_CONNECTION_ENTRY ConEntry;

    // get pin
    Pin = (PKSP_PIN)Request;

    if (RequestLength < sizeof(KSP_PIN))
    {
        // input buffer must be at least sizeof KSP_PIN
        DPRINT1("input length too small\n");
        return STATUS_INVALID_PARAMETER;
    }

    if (IsListEmpty(&Descriptor->PhysicalConnectionList))
    {
        DPRINT1("no connection\n");
        return STATUS_NOT_FOUND;
    }

    // get first item
    Entry = Descriptor->PhysicalConnectionList.Flink;

    do
    {
        ConEntry = (PPHYSICAL_CONNECTION_ENTRY)CONTAINING_RECORD(Entry, PHYSICAL_CONNECTION_ENTRY, Entry);

        if (ConEntry->FromPin == Pin->PinId)
        {
            Connection = (PKSPIN_PHYSICALCONNECTION)Data;
            DPRINT("FoundEntry %S Size %u\n", ConEntry->Connection.SymbolicLinkName, ConEntry->Connection.Size);
            IoStatus->Information = ConEntry->Connection.Size;

            if (!DataLength)
            {
                IoStatus->Information = ConEntry->Connection.Size;
                return STATUS_MORE_ENTRIES;
            }

            if (DataLength < ConEntry->Connection.Size)
            {
                return STATUS_BUFFER_TOO_SMALL;
            }

            RtlMoveMemory(Data, &ConEntry->Connection, ConEntry->Connection.Size);
            return STATUS_SUCCESS;
       }

        // move to next item
        Entry = Entry->Flink;
    }while(Entry != &Descriptor->PhysicalConnectionList);

    IoStatus->Information = 0;
    return STATUS_NOT_FOUND;
}

NTSTATUS
NTAPI
PinPropertyHandler(
    IN PIRP Irp,
    IN PKSIDENTIFIER  Request,
    IN OUT PVOID  Data)
{
    PIO_STACK_LOCATION IoStack;
    //PKSOBJECT_CREATE_ITEM CreateItem;
    PSUBDEVICE_DESCRIPTOR Descriptor;
    IIrpTarget * IrpTarget;
    IPort *Port;
    ISubdevice *SubDevice;


    NTSTATUS Status = STATUS_UNSUCCESSFUL;

    Descriptor = (PSUBDEVICE_DESCRIPTOR)KSPROPERTY_ITEM_IRP_STORAGE(Irp);
    PC_ASSERT(Descriptor);

    // get current irp stack
    IoStack = IoGetCurrentIrpStackLocation(Irp);

    // Get the IrpTarget
    IrpTarget = (IIrpTarget*)IoStack->FileObject->FsContext;
    // Get the parent
    Status = IrpTarget->QueryInterface(IID_IPort, (PVOID*)&Port);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to obtain IPort interface from filter\n");
        Irp->IoStatus.Information = 0;
        Irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
        return STATUS_UNSUCCESSFUL;
    }

    // Get private ISubdevice interface
    Status = Port->QueryInterface(IID_ISubdevice, (PVOID*)&SubDevice);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to obtain ISubdevice interface from port driver\n");
        KeBugCheck(0);
    }

    // get current stack location
    IoStack = IoGetCurrentIrpStackLocation(Irp);

    switch(Request->Id)
    {
        case KSPROPERTY_PIN_CTYPES:
        case KSPROPERTY_PIN_DATAFLOW:
        case KSPROPERTY_PIN_DATARANGES:
        case KSPROPERTY_PIN_INTERFACES:
        case KSPROPERTY_PIN_MEDIUMS:
        case KSPROPERTY_PIN_COMMUNICATION:
        case KSPROPERTY_PIN_CATEGORY:
        case KSPROPERTY_PIN_NAME:
        case KSPROPERTY_PIN_PROPOSEDATAFORMAT:
            Status = KsPinPropertyHandler(Irp, Request, Data, Descriptor->Factory.PinDescriptorCount, Descriptor->Factory.KsPinDescriptor);
            break;
        case KSPROPERTY_PIN_GLOBALCINSTANCES:
            Status = HandlePropertyInstances(&Irp->IoStatus, Request, Data, Descriptor, TRUE);
            break;
        case KSPROPERTY_PIN_CINSTANCES:
            Status = HandlePropertyInstances(&Irp->IoStatus, Request, Data, Descriptor, FALSE);
            break;
        case KSPROPERTY_PIN_NECESSARYINSTANCES:
            Status = HandleNecessaryPropertyInstances(&Irp->IoStatus, Request, Data, Descriptor);
            break;

        case KSPROPERTY_PIN_DATAINTERSECTION:
            Status = HandleDataIntersection(&Irp->IoStatus, Request, Data, IoStack->Parameters.DeviceIoControl.OutputBufferLength, Descriptor, SubDevice);
            break;
        case KSPROPERTY_PIN_PHYSICALCONNECTION:
            Status = HandlePhysicalConnection(&Irp->IoStatus, Request, IoStack->Parameters.DeviceIoControl.InputBufferLength, Data, IoStack->Parameters.DeviceIoControl.OutputBufferLength, Descriptor);
            break;
        case KSPROPERTY_PIN_CONSTRAINEDDATARANGES:
            UNIMPLEMENTED
            Status = STATUS_NOT_IMPLEMENTED;
            break;
        default:
            UNIMPLEMENTED
            Status = STATUS_UNSUCCESSFUL;
    }

    // Release reference
    Port->Release();

    // Release subdevice reference
    SubDevice->Release();

    return Status;
}

NTSTATUS
NTAPI
FastPropertyHandler(
    IN PFILE_OBJECT  FileObject,
    IN PKSPROPERTY UNALIGNED  Property,
    IN ULONG  PropertyLength,
    IN OUT PVOID UNALIGNED  Data,
    IN ULONG  DataLength,
    OUT PIO_STATUS_BLOCK  IoStatus,
    IN ULONG  PropertySetsCount,
    IN const KSPROPERTY_SET *PropertySet,
    IN PSUBDEVICE_DESCRIPTOR Descriptor,
    IN ISubdevice *SubDevice)
{
    PFNKSHANDLER PropertyHandler = NULL;
    NTSTATUS Status;
    KSP_PIN * Pin;
    ULONG Size, Index;
    PKSMULTIPLE_ITEM Item;

    PC_ASSERT(Descriptor);

    if (!IsEqualGUIDAligned(Property->Set, KSPROPSETID_Pin))
    {
        // the fast handler only supports pin properties atm*/
        DPRINT("Only KSPROPSETID_Pin is supported\n");
        IoStatus->Status = Status = STATUS_NOT_IMPLEMENTED;
        IoStatus->Information = 0;
        return Status;
    }

    // property handler is used to verify input parameters
    Status = FindPropertyHandler(IoStatus, Descriptor, Property, PropertyLength, DataLength, Data, &PropertyHandler);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("FindPropertyHandler failed with %x\n", Status);
        IoStatus->Status = Status = Status;
        IoStatus->Information = 0;
        return Status;
    }

    switch(Property->Id)
    {
        case KSPROPERTY_PIN_CTYPES:
            (*(PULONG)Data) = Descriptor->Factory.PinDescriptorCount;
            IoStatus->Information = sizeof(ULONG);
            IoStatus->Status = Status = STATUS_SUCCESS;
            break;
        case KSPROPERTY_PIN_DATAFLOW:
            Pin = (KSP_PIN*)Property;
            if (Pin->PinId >= Descriptor->Factory.PinDescriptorCount)
            {
                IoStatus->Status = Status = STATUS_INVALID_PARAMETER;
                IoStatus->Information = 0;
                break;
            }

            *((KSPIN_DATAFLOW*)Data) = Descriptor->Factory.KsPinDescriptor[Pin->PinId].DataFlow;
            IoStatus->Information = sizeof(KSPIN_DATAFLOW);
            IoStatus->Status = Status = STATUS_SUCCESS;
            break;
        case KSPROPERTY_PIN_COMMUNICATION:
            Pin = (KSP_PIN*)Property;
            if (Pin->PinId >= Descriptor->Factory.PinDescriptorCount)
            {
                IoStatus->Status = Status = STATUS_INVALID_PARAMETER;
                IoStatus->Information = 0;
                break;
            }

            *((KSPIN_COMMUNICATION*)Data) = Descriptor->Factory.KsPinDescriptor[Pin->PinId].Communication;
            IoStatus->Status = Status = STATUS_SUCCESS;
            IoStatus->Information = sizeof(KSPIN_COMMUNICATION);
            break;
        case KSPROPERTY_PIN_DATARANGES:
            Pin = (KSP_PIN*)Property;
            if (Pin->PinId >= Descriptor->Factory.PinDescriptorCount)
            {
                IoStatus->Status = Status = STATUS_INVALID_PARAMETER;
                IoStatus->Information = 0;
                break;
            }
            Size = sizeof(KSMULTIPLE_ITEM);
            for (Index = 0; Index < Descriptor->Factory.KsPinDescriptor[Pin->PinId].DataRangesCount; Index++)
            {
                Size += Descriptor->Factory.KsPinDescriptor[Pin->PinId].DataRanges[Index]->FormatSize;
            }

            if (DataLength < Size)
            {
                IoStatus->Information = Size;
                IoStatus->Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            Item = (KSMULTIPLE_ITEM*)Data;
            Item->Size = Size;
            Item->Count = Descriptor->Factory.KsPinDescriptor[Pin->PinId].DataRangesCount;

            Data = (PUCHAR)(Item +1);
            for (Index = 0; Index < Descriptor->Factory.KsPinDescriptor[Pin->PinId].DataRangesCount; Index++)
            {
                RtlMoveMemory(Data, Descriptor->Factory.KsPinDescriptor[Pin->PinId].DataRanges[Index], Descriptor->Factory.KsPinDescriptor[Pin->PinId].DataRanges[Index]->FormatSize);
                Data = ((PUCHAR)Data + Descriptor->Factory.KsPinDescriptor[Pin->PinId].DataRanges[Index]->FormatSize);
            }

            IoStatus->Status = Status = STATUS_SUCCESS;
            IoStatus->Information = Size;
            break;

        case KSPROPERTY_PIN_GLOBALCINSTANCES:
            Status = HandlePropertyInstances(IoStatus, Property, Data, Descriptor, TRUE);
            break;
        case KSPROPERTY_PIN_CINSTANCES:
            Status = HandlePropertyInstances(IoStatus, Property, Data, Descriptor, FALSE);
            break;
        case KSPROPERTY_PIN_NECESSARYINSTANCES:
            Status = HandleNecessaryPropertyInstances(IoStatus, Property, Data, Descriptor);
            break;

        case KSPROPERTY_PIN_DATAINTERSECTION:
            Status = HandleDataIntersection(IoStatus, Property, Data, DataLength, Descriptor, SubDevice);
            break;
        case KSPROPERTY_PIN_PHYSICALCONNECTION:
        case KSPROPERTY_PIN_CONSTRAINEDDATARANGES:
        case KSPROPERTY_PIN_INTERFACES:
        case KSPROPERTY_PIN_MEDIUMS:
        case KSPROPERTY_PIN_CATEGORY:
        case KSPROPERTY_PIN_NAME:
        case KSPROPERTY_PIN_PROPOSEDATAFORMAT:
            UNIMPLEMENTED
            IoStatus->Status = Status = STATUS_NOT_IMPLEMENTED;
            IoStatus->Information = 0;
            break;
        default:
            UNIMPLEMENTED
            IoStatus->Status = Status = STATUS_NOT_IMPLEMENTED;
            IoStatus->Information = 0;
    }
    return Status;
}

NTSTATUS
NTAPI
TopologyPropertyHandler(
    IN PIRP Irp,
    IN PKSIDENTIFIER  Request,
    IN OUT PVOID  Data)
{
    PSUBDEVICE_DESCRIPTOR Descriptor;

    Descriptor = (PSUBDEVICE_DESCRIPTOR)KSPROPERTY_ITEM_IRP_STORAGE(Irp);

    return KsTopologyPropertyHandler(Irp, Request, Data, Descriptor->Topology);
}

NTSTATUS
FindPropertyHandler(
    IN PIO_STATUS_BLOCK IoStatus,
    IN PSUBDEVICE_DESCRIPTOR Descriptor,
    IN PKSPROPERTY Property,
    IN ULONG InputBufferLength,
    IN ULONG OutputBufferLength,
    OUT PVOID OutputBuffer,
    OUT PFNKSHANDLER *PropertyHandler)
{
    ULONG Index, ItemIndex;
    PULONG Flags;
    PKSPROPERTY_DESCRIPTION Description;

    for(Index = 0; Index < Descriptor->FilterPropertySet.FreeKsPropertySetOffset; Index++)
    {
        if (IsEqualGUIDAligned(Property->Set, *Descriptor->FilterPropertySet.Properties[Index].Set))
        {
            for(ItemIndex = 0; ItemIndex < Descriptor->FilterPropertySet.Properties[Index].PropertiesCount; ItemIndex++)
            {
                if (Descriptor->FilterPropertySet.Properties[Index].PropertyItem[ItemIndex].PropertyId == Property->Id)
                {
                    if (Property->Flags & KSPROPERTY_TYPE_SET)
                        *PropertyHandler = Descriptor->FilterPropertySet.Properties[Index].PropertyItem[ItemIndex].SetPropertyHandler;

                    if (Property->Flags & KSPROPERTY_TYPE_GET)
                        *PropertyHandler = Descriptor->FilterPropertySet.Properties[Index].PropertyItem[ItemIndex].GetPropertyHandler;

                    if (Property->Flags & KSPROPERTY_TYPE_BASICSUPPORT)
                    {
                        if (sizeof(ULONG) > OutputBufferLength)
                        {
                            // too small buffer
                            return STATUS_INVALID_PARAMETER;
                        }

                        // get output buffer
                        Flags = (PULONG)OutputBuffer;

                        // clear flags
                        *Flags = KSPROPERTY_TYPE_BASICSUPPORT;

                        if (Descriptor->FilterPropertySet.Properties[Index].PropertyItem[ItemIndex].GetSupported)
                            *Flags |= KSPROPERTY_TYPE_GET;

                        if (Descriptor->FilterPropertySet.Properties[Index].PropertyItem[ItemIndex].SetSupported)
                            *Flags |= KSPROPERTY_TYPE_SET;

                        IoStatus->Information = sizeof(ULONG);

                        if (OutputBufferLength >= sizeof(KSPROPERTY_DESCRIPTION))
                        {
                            // get output buffer
                            Description = (PKSPROPERTY_DESCRIPTION)OutputBuffer;

                            // store result
                            Description->DescriptionSize = sizeof(KSPROPERTY_DESCRIPTION);
                            Description->PropTypeSet.Set = KSPROPTYPESETID_General;
                            Description->PropTypeSet.Id = 0;
                            Description->PropTypeSet.Flags = 0;
                            Description->MembersListCount = 0;
                            Description->Reserved = 0;

                            IoStatus->Information = sizeof(KSPROPERTY_DESCRIPTION);
                        }

                        return STATUS_SUCCESS;
                    }


                    if (Descriptor->FilterPropertySet.Properties[Index].PropertyItem[ItemIndex].MinProperty > InputBufferLength)
                    {
                        // too small input buffer
                        IoStatus->Information = Descriptor->FilterPropertySet.Properties[Index].PropertyItem[ItemIndex].MinProperty;
                        IoStatus->Status = STATUS_BUFFER_TOO_SMALL;
                        return STATUS_BUFFER_TOO_SMALL;
                    }

                    if (Descriptor->FilterPropertySet.Properties[Index].PropertyItem[ItemIndex].MinData > OutputBufferLength)
                    {
                        // too small output buffer
                        IoStatus->Information = Descriptor->FilterPropertySet.Properties[Index].PropertyItem[ItemIndex].MinData;
                        IoStatus->Status = STATUS_BUFFER_TOO_SMALL;
                        return STATUS_BUFFER_TOO_SMALL;
                    }
                    return STATUS_SUCCESS;
                }
            }
        }
    }
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS
PcCountProperties(
    IN PIRP Irp,
    IN PSUBDEVICE_DESCRIPTOR Descriptor)
{
    ULONG Properties;
    ULONG Index, Offset;
    PIO_STACK_LOCATION IoStack;
    LPGUID Guid;

    // count property items
    Properties = Descriptor->FilterPropertySet.FreeKsPropertySetOffset;

    if (Descriptor->DeviceDescriptor->AutomationTable)
    {
        Properties = Descriptor->DeviceDescriptor->AutomationTable->PropertyCount;
    }

    // get current irp stack
    IoStack = IoGetCurrentIrpStackLocation(Irp);

    // store output size
    Irp->IoStatus.Information = sizeof(GUID) * Properties;

    if (IoStack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(GUID) * Properties)
    {
        // buffer too small
        Irp->IoStatus.Status = STATUS_BUFFER_OVERFLOW;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);

        return STATUS_BUFFER_OVERFLOW;
    }

    // get output buffer
    Guid = (LPGUID)Irp->UserBuffer;


    // copy property guids from filter
    Offset = 0;
    for(Index = 0; Index < Descriptor->FilterPropertySet.FreeKsPropertySetOffset; Index++)
    {
        RtlMoveMemory(&Guid[Offset], Descriptor->FilterPropertySet.Properties[Index].Set, sizeof(GUID));
        Offset++;
    }

    if (Descriptor->DeviceDescriptor->AutomationTable)
    {
        // copy property guids from driver
        for(Index = 0; Index < Descriptor->DeviceDescriptor->AutomationTable->PropertyCount; Index++)
        {
            RtlMoveMemory(&Guid[Offset], Descriptor->DeviceDescriptor->AutomationTable->Properties[Index].Set, sizeof(GUID));
            Offset++;
        }
    }

     // done
     Irp->IoStatus.Status = STATUS_SUCCESS;
     IoCompleteRequest(Irp, IO_NO_INCREMENT);

     return STATUS_SUCCESS;
}


NTSTATUS
NTAPI
PcPropertyHandler(
    IN PIRP Irp,
    IN PSUBDEVICE_DESCRIPTOR Descriptor)
{
    ULONG Index;
    PIO_STACK_LOCATION IoStack;
    PKSPROPERTY Property;
    PFNKSHANDLER PropertyHandler = NULL;
    UNICODE_STRING GuidString;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    PPCPROPERTY_REQUEST PropertyRequest;

    IoStack = IoGetCurrentIrpStackLocation(Irp);

    Property = (PKSPROPERTY)IoStack->Parameters.DeviceIoControl.Type3InputBuffer;
    PC_ASSERT(Property);

    if (IsEqualGUIDAligned(Property->Set, GUID_NULL) && Property->Id == 0 && Property->Flags == KSPROPERTY_TYPE_SETSUPPORT)
    {
        return PcCountProperties(Irp, Descriptor);
    }


    // check properties provided by the driver
    if (Descriptor->DeviceDescriptor->AutomationTable)
    {
        for(Index = 0; Index < Descriptor->DeviceDescriptor->AutomationTable->PropertyCount; Index++)
        {
            if (IsEqualGUID(*Descriptor->DeviceDescriptor->AutomationTable->Properties[Index].Set, Property->Set))
            {
                if (Descriptor->DeviceDescriptor->AutomationTable->Properties[Index].Id == Property->Id)
                {
                    if(Descriptor->DeviceDescriptor->AutomationTable->Properties[Index].Flags & Property->Flags)
                    {
                        PropertyRequest = (PPCPROPERTY_REQUEST)ExAllocatePool(NonPagedPool, sizeof(PCPROPERTY_REQUEST));
                        if (!PropertyRequest)
                        {
                            // no memory
                            Irp->IoStatus.Information = 0;
                            Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                            IoCompleteRequest(Irp, IO_NO_INCREMENT);
                            return STATUS_INSUFFICIENT_RESOURCES;
                        }
                        RtlZeroMemory(PropertyRequest, sizeof(PCPROPERTY_REQUEST));
                        PropertyRequest->PropertyItem = &Descriptor->DeviceDescriptor->AutomationTable->Properties[Index];
                        PropertyRequest->Verb = Property->Flags;
                        PropertyRequest->Value = Irp->UserBuffer;
                        PropertyRequest->ValueSize = IoStack->Parameters.DeviceIoControl.OutputBufferLength;
                        PropertyRequest->Irp = Irp;

                        DPRINT("Calling handler %p\n", Descriptor->DeviceDescriptor->AutomationTable->Properties[Index].Handler);
                        Status = Descriptor->DeviceDescriptor->AutomationTable->Properties[Index].Handler(PropertyRequest);

                        Irp->IoStatus.Status = Status;
                        IoCompleteRequest(Irp, IO_NO_INCREMENT);
                        return Status;
                    }
                }
            }
        }
    }

    Status = FindPropertyHandler(&Irp->IoStatus, Descriptor, Property, IoStack->Parameters.DeviceIoControl.InputBufferLength, IoStack->Parameters.DeviceIoControl.OutputBufferLength, Irp->UserBuffer, &PropertyHandler);
    if (NT_SUCCESS(Status) && PropertyHandler)
    {
        // HACK
        KSPROPERTY_ITEM_IRP_STORAGE(Irp) = (const KSPROPERTY_ITEM*)Descriptor;
        DPRINT("Calling property handler %p\n", PropertyHandler);
        Status = PropertyHandler(Irp, Property, Irp->UserBuffer);
    }
    else if (!NT_SUCCESS(Status))
    {
        RtlStringFromGUID(Property->Set, &GuidString);
        DPRINT1("Unhandeled property: Set %S Id %u Flags %x InputLength %u OutputLength %u\n", GuidString.Buffer, Property->Id, Property->Flags, IoStack->Parameters.DeviceIoControl.InputBufferLength, IoStack->Parameters.DeviceIoControl.OutputBufferLength);
        RtlFreeUnicodeString(&GuidString);
    }

    // the information member is set by the handler
    Irp->IoStatus.Status = Status;
    DPRINT("Result %x Length %u\n", Status, Irp->IoStatus.Information);
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

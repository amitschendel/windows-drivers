#include <ntifs.h>
#include <ntddk.h>
#include "PriorityBoosterCommon.h"

void PriorityBoosterUnload(_In_ PDRIVER_OBJECT DriverObject);
NTSTATUS PriorityBoosterCreateClose(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);
NTSTATUS PriorityBoosterDeviceControl(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);


extern "C"
NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
	UNREFERENCED_PARAMETER(RegistryPath);
	DriverObject->DriverUnload = PriorityBoosterUnload;
	DriverObject->MajorFunction[IRP_MJ_CREATE] = PriorityBoosterCreateClose;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = PriorityBoosterCreateClose;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = PriorityBoosterDeviceControl;

	UNICODE_STRING devName = RTL_CONSTANT_STRING(L"\\Device\\PriorityBooster");
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\PriorityBooster");

	PDEVICE_OBJECT DeviceObject;
	NTSTATUS status = IoCreateDevice(
		DriverObject, // our driver object,
		0, // no need for extra bytes,
		&devName, // the device name,
		FILE_DEVICE_UNKNOWN, // device type,
		0, // characteristics flags,
		FALSE, // not exclusive,
		&DeviceObject // the resulting pointer
	);
	if (!NT_SUCCESS(status)) {
		KdPrint(("Failed to create device object (0x%08X)\n", status));
		return status;
	}
	
	status = IoCreateSymbolicLink(&symLink, &devName);
	if (!NT_SUCCESS(status)) {
		KdPrint(("Failed to create symbolic link (0x%08X)\n", status));
		IoDeleteDevice(DeviceObject);
		return status;
	}

	return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS PriorityBoosterCreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
	UNREFERENCED_PARAMETER(DeviceObject);
	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS PriorityBoosterDeviceControl(PDEVICE_OBJECT, PIRP Irp) {
	// get our IO_STACK_LOCATION
	auto stack = IoGetCurrentIrpStackLocation(Irp); // IO_STACK_LOCATION*
	auto status = STATUS_SUCCESS;
	switch (stack->Parameters.DeviceIoControl.IoControlCode) {
		case IOCTL_PRIORITY_BOOSTER_SET_PRIORITY: {
			// do the work
			if (stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(ThreadData)) {
				status = STATUS_BUFFER_TOO_SMALL;
				break;
			}
			auto data = (ThreadData*)stack->Parameters.DeviceIoControl.Type3InputBuffer;
			if (data == nullptr) {
				status = STATUS_INVALID_PARAMETER;
				break;
			}
			if (data->Priority < 1 || data->Priority > 31) {
				status = STATUS_INVALID_PARAMETER;
				break;
			}
			PETHREAD Thread;
			status = PsLookupThreadByThreadId(ULongToHandle(data->ThreadId), &Thread);
			if (!NT_SUCCESS(status))
				break;
			KeSetPriorityThread((PKTHREAD)Thread, data->Priority);
			ObDereferenceObject(Thread);
			KdPrint(("Thread Priority change for %d to %d succeeded!\n", data->ThreadId, data->Priority));
			break;
		}
		default:
			status = STATUS_INVALID_DEVICE_REQUEST;
			break;
		}
	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return status;

}


void PriorityBoosterUnload(_In_ PDRIVER_OBJECT DriverObject)
{
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\PriorityBooster");
	// delete symbolic link
	IoDeleteSymbolicLink(&symLink);
	// delete device object
	IoDeleteDevice(DriverObject->DeviceObject);

}

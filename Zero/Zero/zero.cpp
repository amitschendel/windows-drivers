#include "pch.h"
#include "zeroCommon.h"

#define DRIVER_PREFIX "Zero: "

// prototypes

void ZeroUnload(PDRIVER_OBJECT DriverObject);
DRIVER_DISPATCH ZeroCreateClose, ZeroRead, ZeroWrite, ZeroDeviceControl;
NTSTATUS CompleteIrp(PIRP Irp, NTSTATUS status = STATUS_SUCCESS, ULONG_PTR info = 0);
ReadWriteBytesCount dataCount;

// DriverEntry
EXTERN_C NTSTATUS
DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
	UNREFERENCED_PARAMETER(RegistryPath);

	DriverObject->DriverUnload = ZeroUnload;
	DriverObject->MajorFunction[IRP_MJ_CREATE] = ZeroCreateClose;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = ZeroCreateClose;
	DriverObject->MajorFunction[IRP_MJ_READ] = ZeroRead;
	DriverObject->MajorFunction[IRP_MJ_WRITE] = ZeroWrite;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = ZeroDeviceControl;

	UNICODE_STRING devName = RTL_CONSTANT_STRING(L"\\Device\\Zero");
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\Zero");
	PDEVICE_OBJECT DeviceObject = nullptr;
	auto status = STATUS_SUCCESS;

	do {
		status = IoCreateDevice(DriverObject, 0, &devName, FILE_DEVICE_UNKNOWN, 0, FALSE, &DeviceObject);
		if (!NT_SUCCESS(status)) {
			KdPrint((DRIVER_PREFIX "failed to create device (0x%08X)\n", status));
			break;
		}
		// set up Direct I/O
		DeviceObject->Flags |= DO_DIRECT_IO;

		status = IoCreateSymbolicLink(&symLink, &devName);
		if (!NT_SUCCESS(status)) {
			KdPrint((DRIVER_PREFIX "failed to create symbolic link (0x%08X)\n", status));
			break;
		}
	} while (false);

	if (!NT_SUCCESS(status)) {
		if (DeviceObject)
			IoDeleteDevice(DeviceObject);
	}
	return status;
}

NTSTATUS CompleteIrp(PIRP Irp, NTSTATUS status = STATUS_SUCCESS, ULONG_PTR info = 0) {
	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = info;
	IoCompleteRequest(Irp, 0);
	return status;
}

NTSTATUS ZeroCreateClose(PDEVICE_OBJECT, PIRP Irp) {
	return CompleteIrp(Irp);
}

NTSTATUS ZeroRead(PDEVICE_OBJECT, PIRP Irp) {
	auto stack = IoGetCurrentIrpStackLocation(Irp);
	auto len = stack->Parameters.Read.Length;
	if (len == 0)
		return CompleteIrp(Irp, STATUS_INVALID_BUFFER_SIZE);
	auto buffer = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
	if (!buffer)
		return CompleteIrp(Irp, STATUS_INSUFFICIENT_RESOURCES);
	memset(buffer, 0, len);
	_InlineInterlockedAdd64(&dataCount.readBytes, len);

	return CompleteIrp(Irp, STATUS_SUCCESS, len);
}

NTSTATUS ZeroWrite(PDEVICE_OBJECT, PIRP Irp) {
	auto stack = IoGetCurrentIrpStackLocation(Irp);
	auto len = stack->Parameters.Write.Length;
	_InlineInterlockedAdd64(&dataCount.writeBytes, len);

	return CompleteIrp(Irp, STATUS_SUCCESS, len);
}

NTSTATUS ZeroDeviceControl(PDEVICE_OBJECT, PIRP Irp) {
	auto stack = IoGetCurrentIrpStackLocation(Irp);
	auto status = STATUS_SUCCESS;
	switch (stack->Parameters.DeviceIoControl.IoControlCode) {
		case IOCTL_ZERO_QUERY_BYTES_COUNT: {
			if (stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(ReadWriteBytesCount)) {
				status = STATUS_BUFFER_TOO_SMALL;
				break;
			}
			auto data = (ReadWriteBytesCount*)stack->Parameters.DeviceIoControl.Type3InputBuffer;
			if (data == nullptr) {
				status = STATUS_INVALID_PARAMETER;
				break;
			}
			data->readBytes = dataCount.readBytes;
			data->writeBytes = dataCount.writeBytes;
			break;
		}
		default:
			status = STATUS_INVALID_DEVICE_REQUEST;
			break;
	}

	return CompleteIrp(Irp, status, sizeof(ReadWriteBytesCount));
}
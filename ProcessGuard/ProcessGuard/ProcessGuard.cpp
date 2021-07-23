#include "pch.h"
#include "ProcessGuard.h"
#include "ProcessGuardCommon.h"
#include "AutoLock.h"


DRIVER_UNLOAD ProcessGuardUnload;
DRIVER_DISPATCH ProcessGuardCreateClose;
NTSTATUS ProcessGuardDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp);
void OnProcessNotify(PEPROCESS Process, HANDLE ProcessId, PPS_CREATE_NOTIFY_INFO CreateInfo);
void PushItem(LIST_ENTRY* entry);

Globals g_Globals;

extern "C"
NTSTATUS
DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
	UNREFERENCED_PARAMETER(RegistryPath);
	// Init
	g_Globals.Mutex.Init();
	g_Globals.ForbiddenProcessesCount = 0;
	InitializeListHead(&g_Globals.ForbiddenProcessesHead);
	auto status = STATUS_SUCCESS;

	PDEVICE_OBJECT DeviceObject = nullptr;
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\ProcessGuard");
	bool symLinkCreated = false;
	do {
		UNICODE_STRING devName = RTL_CONSTANT_STRING(L"\\Device\\ProcessGuard");
		status = IoCreateDevice(DriverObject, 0, &devName, FILE_DEVICE_UNKNOWN, 0, TRUE, &DeviceObject);
		if (!NT_SUCCESS(status)) {
			KdPrint((DRIVER_PREFIX "failed to create device (0x%08X)\n", status));
			break;
		}

		status = IoCreateSymbolicLink(&symLink, &devName);
		if (!NT_SUCCESS(status)) {
			KdPrint((DRIVER_PREFIX "failed to create sym link (0x%08X)\n", status));
			break;
		}
		symLinkCreated = true;

		// Register for process notifications
		status = PsSetCreateProcessNotifyRoutineEx(OnProcessNotify, FALSE);
		if (!NT_SUCCESS(status)) {
			KdPrint((DRIVER_PREFIX "failed to register for process callbacks (0x%08X)\n", status));
			break;
		}

	} while (false);

	if (!NT_SUCCESS(status)) {
		if (symLinkCreated)
			IoDeleteSymbolicLink(&symLink);
		if (DeviceObject)
			IoDeleteDevice(DeviceObject);
	}

	DriverObject->DriverUnload = ProcessGuardUnload;
	DriverObject->MajorFunction[IRP_MJ_CREATE] = DriverObject->MajorFunction[IRP_MJ_CLOSE] = ProcessGuardCreateClose;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = ProcessGuardDeviceControl;

	return STATUS_SUCCESS;
}

void OnProcessNotify(PEPROCESS Process, HANDLE ProcessId, PPS_CREATE_NOTIFY_INFO CreateInfo) {
	UNREFERENCED_PARAMETER(Process);
	UNREFERENCED_PARAMETER(ProcessId);
	
	if (CreateInfo) {
		if (CreateInfo->FileOpenNameAvailable && CreateInfo->ImageFileName) {
			AutoLock lock(g_Globals.Mutex);
			if (g_Globals.ForbiddenProcessesCount > 0) {
				DbgPrint("Forbidden Processes Count: -> %d", g_Globals.ForbiddenProcessesCount);
				PLIST_ENTRY temp = nullptr;
				temp = &g_Globals.ForbiddenProcessesHead;
				temp = temp->Flink;
				while (&g_Globals.ForbiddenProcessesHead != temp) {
					auto item = CONTAINING_RECORD(temp, ForbiddenProcess<UNICODE_STRING>, Entry);
					DbgPrint("Address at iteration -> %p", &item->Entry);
					DbgPrint("Current forbidden process -> %wZ", &item->ProcessPath);
					DbgPrint("Image name -> %wZ", CreateInfo->ImageFileName);
					if (0 == RtlCompareUnicodeString(CreateInfo->ImageFileName, &item->ProcessPath, TRUE)) {
						DbgPrint("Found Forbidden Process -> %wZ", item->ProcessPath);
						CreateInfo->CreationStatus = STATUS_ACCESS_DENIED;
						break;
					}
					temp = temp->Flink;
				}
			}
		}
	}
}

void ProcessGuardUnload(PDRIVER_OBJECT DriverObject) {
	// Unregister process notifications
	PsSetCreateProcessNotifyRoutineEx(OnProcessNotify, TRUE);

	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\ProcessGuard");
	IoDeleteSymbolicLink(&symLink);
	IoDeleteDevice(DriverObject->DeviceObject);

	// Free list
	while (!IsListEmpty(&g_Globals.ForbiddenProcessesHead)) {
		auto entry = RemoveHeadList(&g_Globals.ForbiddenProcessesHead);
		auto item = CONTAINING_RECORD(entry, ForbiddenProcess<UNICODE_STRING>, Entry);
		RtlFreeUnicodeString(&item->ProcessPath);
		ExFreePool(item);
	}
}

NTSTATUS ProcessGuardCreateClose(PDEVICE_OBJECT, PIRP Irp) {
	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, 0);
	return STATUS_SUCCESS;
}

NTSTATUS ProcessGuardDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
	UNREFERENCED_PARAMETER(DeviceObject);
	auto stack = IoGetCurrentIrpStackLocation(Irp);
	auto status = STATUS_SUCCESS;
	auto size = sizeof(ForbiddenProcess<UNICODE_STRING>);
	auto info = (ForbiddenProcess<UNICODE_STRING>*)ExAllocatePoolWithTag(PagedPool, size, DRIVER_TAG);
	if (info == nullptr) {
		KdPrint((DRIVER_PREFIX "failed allocation\n"));
		status = STATUS_INSUFFICIENT_RESOURCES;
		Irp->IoStatus.Status = status;
		Irp->IoStatus.Information = 0;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return status;
	}
	switch (stack->Parameters.DeviceIoControl.IoControlCode)
	{
		case IOCTL_PROCESS_GUARD_FORBIDDEN_PROCESS: {
			if (stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(ProcessData)) {
				status = STATUS_BUFFER_TOO_SMALL;
				break;
			}
			auto data = (ProcessData*)stack->Parameters.DeviceIoControl.Type3InputBuffer;
			if (data == nullptr) {
				status = STATUS_INVALID_PARAMETER;
				break;
			}
			RtlCreateUnicodeString(&info->ProcessPath, data->ProcessPath);
			DbgPrint("Received Process -> %wZ", info->ProcessPath);
			DbgPrint("Inserting to list -> %p", &info->Entry);
			PushItem(&info->Entry);
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

void PushItem(LIST_ENTRY* entry) {
	AutoLock<FastMutex> lock(g_Globals.Mutex);
	if (g_Globals.ForbiddenProcessesCount > MAX_FORBIDDEN_PROCESSES) {
		// Too many items, remove oldest one
		auto head = RemoveHeadList(&g_Globals.ForbiddenProcessesHead);
		g_Globals.ForbiddenProcessesCount--;
		auto item = CONTAINING_RECORD(head, ForbiddenProcess<UNICODE_STRING>, Entry);
		RtlFreeUnicodeString(&item->ProcessPath);
		ExFreePool(item);
	}
	InsertTailList(&g_Globals.ForbiddenProcessesHead, entry);
	g_Globals.ForbiddenProcessesCount++;
}

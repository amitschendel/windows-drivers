#include "pch.h"
#include "ProcessGuard.h"
//#include "Utils.h"
#include "ProcessGuardCommon.h"
#include "AutoLock.h"

DRIVER_UNLOAD ProcessGuardUnload;
DRIVER_DISPATCH ProcessGuardCreateClose; //ProcessGuardWrite;
NTSTATUS ProcessGuardDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp);
void OnProcessNotify(PEPROCESS Process, HANDLE ProcessId, PPS_CREATE_NOTIFY_INFO CreateInfo);
void PushItem(LIST_ENTRY* entry);
bool IsForbiddenProcess(PCUNICODE_STRING imageFileName);

Globals g_Globals;
//auto g_Globals = *(Globals*)ExAllocatePoolWithTag(NonPagedPool, sizeof(Globals), DRIVER_TAG);

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
		//DeviceObject->Flags |= DO_BUFFERED_IO;

		status = IoCreateSymbolicLink(&symLink, &devName);
		if (!NT_SUCCESS(status)) {
			KdPrint((DRIVER_PREFIX "failed to create sym link (0x%08X)\n", status));
			break;
		}
		symLinkCreated = true;

		// register for process notifications
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
	//DriverObject->MajorFunction[IRP_MJ_WRITE] = ProcessGuardWrite;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = ProcessGuardDeviceControl;

	return STATUS_SUCCESS;
}

void OnProcessNotify(PEPROCESS Process, HANDLE ProcessId, PPS_CREATE_NOTIFY_INFO CreateInfo) {
	UNREFERENCED_PARAMETER(Process);
	UNREFERENCED_PARAMETER(ProcessId);
	//UNICODE_STRING FORBIDDEN_PROCESS_PATH = RTL_CONSTANT_STRING(L"\\??\\C:\\Windows\\system32\\notepad.exe");
	if (CreateInfo) {
		if (CreateInfo->FileOpenNameAvailable && CreateInfo->ImageFileName) {
			//DbgPrint("Image -> %wZ", CreateInfo->ImageFileName);
			//DbgPrint("My -> %wZ", &FORBIDDEN_PROCESS_PATH);
			
			/*if (0 == RtlCompareUnicodeString(CreateInfo->ImageFileName, &FORBIDDEN_PROCESS_PATH, TRUE)) {
				DbgPrint("Terminating Notepad Process");
				CreateInfo->CreationStatus = STATUS_ACCESS_DENIED;
			}*/

			AutoLock lock(g_Globals.Mutex);
			if (g_Globals.ForbiddenProcessesCount > 0) {
				PLIST_ENTRY temp = nullptr;
				temp = &g_Globals.ForbiddenProcessesHead;
				//int count = 0;

				while (&g_Globals.ForbiddenProcessesHead != temp->Flink) {
					temp = temp->Flink;
					UNICODE_STRING s = CONTAINING_RECORD(temp, ForbiddenProcess<UNICODE_STRING>, Entry)->ProcessPath;//BUG IT'S EMPTY
					DbgPrint("Found Forbidden Process -> %wZ", &s);
					//auto entry = RemoveHeadList(&g_Globals.ForbiddenProcessesHead);
					//auto info = CONTAINING_RECORD(entry, ForbiddenProcess<UNICODE_STRING>, Entry);
					//InsertHeadList(&g_Globals.ForbiddenProcessesHead, entry);
					//InsertTailList(&g_Globals.ForbiddenProcessesHead, entry);
					//AutoLock unlock(g_Globals.Mutex);
					if (0 == RtlCompareUnicodeString(CreateInfo->ImageFileName, &s, TRUE)) {
						DbgPrint("Found Forbidden Process -> %wZ", &s);
						CreateInfo->CreationStatus = STATUS_ACCESS_DENIED;
						break;
					}
					//ExFreePool(info);
				}
			}
			//DbgPrint("A new process was created");
			/*if (IsForbiddenProcess(CreateInfo->ImageFileName)) {
				CreateInfo->CreationStatus = STATUS_ACCESS_DENIED;
			}*/
		}
	}
}

void ProcessGuardUnload(PDRIVER_OBJECT DriverObject) {
	// unregister process notifications
	PsSetCreateProcessNotifyRoutineEx(OnProcessNotify, TRUE);

	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\ProcessGuard");
	IoDeleteSymbolicLink(&symLink);
	IoDeleteDevice(DriverObject->DeviceObject);

	// free list
	while (!IsListEmpty(&g_Globals.ForbiddenProcessesHead)) {
		auto entry = RemoveHeadList(&g_Globals.ForbiddenProcessesHead);
		ExFreePool(CONTAINING_RECORD(entry, ForbiddenProcess<UNICODE_STRING>, Entry));
	}
}

NTSTATUS ProcessGuardCreateClose(PDEVICE_OBJECT, PIRP Irp) {
	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, 0);
	return STATUS_SUCCESS;
}

//NTSTATUS ProcessGuardWrite(PDEVICE_OBJECT, PIRP Irp) {
//	auto stack = IoGetCurrentIrpStackLocation(Irp);
//	auto len = stack->Parameters.Write.Length;
//	auto status = STATUS_SUCCESS;
//	auto size = sizeof(ForbiddenProcess<PUNICODE_STRING>);
//	auto info = (ForbiddenProcess<PUNICODE_STRING>*)ExAllocatePoolWithTag(PagedPool, size, DRIVER_TAG);
//	if (info == nullptr) {
//		KdPrint((DRIVER_PREFIX "failed allocation\n"));
//		status = STATUS_INSUFFICIENT_RESOURCES;
//		Irp->IoStatus.Status = status;
//		Irp->IoStatus.Information = len;
//		IoCompleteRequest(Irp, 0);
//		return status;
//	}
//	//NT_ASSERT(Irp->MdlAddress);
//	DbgPrint("PASSED");
//	auto buffer = (WCHAR*)Irp->AssociatedIrp.SystemBuffer;
//
//	////auto buffer = (WCHAR*)MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
//	//UNICODE_STRING buffer = RTL_CONSTANT_STRING(L"\\??\\C:\\Windows\\system32\\notepad.exe");
//	if (!buffer) {
//		DbgPrint("no buffer");
//		status = STATUS_INVALID_PARAMETER;
//	}
//	else {
//		//auto& item = info->ProcessPath;
//		//RtlCopyMemory(item, buffer, len);
//		//RtlCopyMemory(info->ProcessPath, buffer, sizeof(buffer));
//		DbgPrint("process path -> %wZ", buffer);
//		//PushItem(&info->Entry, g_Globals);
//	}
//	Irp->IoStatus.Status = status;
//	Irp->IoStatus.Information = len;
//	IoCompleteRequest(Irp, 0);
//	return status;
//}

NTSTATUS ProcessGuardDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
	UNREFERENCED_PARAMETER(DeviceObject);
	auto stack = IoGetCurrentIrpStackLocation(Irp);
	//auto len = stack->Parameters.Write.Length;
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
			//UNICODE_STRING tS = RTL_CONSTANT_STRING(data->ProcessPath);
			RtlInitUnicodeString(&info->ProcessPath, data->ProcessPath);
			//RtlCopyMemory(info->ProcessPath, &tS, sizeof(tS));
			//info->ProcessPath = data->ProcessPath;
			DbgPrint("Received Process -> %wZ", info->ProcessPath);
			//DbgPrint("Received Process -> %wZ", &tS);
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
	if (g_Globals.ForbiddenProcessesCount > 1024) {
		// too many items, remove oldest one
		auto head = RemoveHeadList(&g_Globals.ForbiddenProcessesHead);
		g_Globals.ForbiddenProcessesCount--;
		auto item = CONTAINING_RECORD(head, ForbiddenProcess<UNICODE_STRING>, Entry);
		ExFreePool(item);
	}
	InsertTailList(&g_Globals.ForbiddenProcessesHead, entry);
	g_Globals.ForbiddenProcessesCount++;
}

bool IsForbiddenProcess(PCUNICODE_STRING imageFileName) {
	AutoLock lock(g_Globals.Mutex);
	int count = 0;
	bool isForbidden = false;
	while (!IsListEmpty(&g_Globals.ForbiddenProcessesHead) && count < g_Globals.ForbiddenProcessesCount) {
		auto entry = RemoveHeadList(&g_Globals.ForbiddenProcessesHead);
		auto info = CONTAINING_RECORD(entry, ForbiddenProcess<UNICODE_STRING>, Entry);
		InsertHeadList(&g_Globals.ForbiddenProcessesHead, entry);
		if (0 == RtlCompareUnicodeString(imageFileName, &info->ProcessPath, TRUE)) {
			DbgPrint("Found Forbidden Process -> %wZ", info->ProcessPath);
			isForbidden = true;
			break;
		}
		count++;
		ExFreePool(info);
	}
	return isForbidden;
}
#include "pch.h"


void SampleUnload(_In_ PDRIVER_OBJECT DriverObject) {
	UNREFERENCED_PARAMETER(DriverObject);
}

void versionInformation() {
	PRTL_OSVERSIONINFOW lpVersionInformation = NULL;
	RtlGetVersion(lpVersionInformation);
	KdPrint(("%lu\n", lpVersionInformation->dwBuildNumber));
}

extern "C"
NTSTATUS
DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath) {
	UNREFERENCED_PARAMETER(DriverObject);
	UNREFERENCED_PARAMETER(RegistryPath);

	DriverObject->DriverUnload = SampleUnload;


	return STATUS_SUCCESS;
}

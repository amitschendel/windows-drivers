#pragma once
#include "FastMutex.h"

#define DRIVER_PREFIX "ProcessGuard: "
#define DRIVER_TAG 'pg'
#define MAX_FORBIDDEN_PROCESSES 1024


template<typename T>
struct ForbiddenProcess
{
	LIST_ENTRY Entry;
	T ProcessPath;
};

struct Globals {
	LIST_ENTRY ForbiddenProcessesHead;
	int ForbiddenProcessesCount;
	FastMutex Mutex;
};
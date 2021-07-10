#pragma once

#define ZERO_DEVICE 0x8000
#define IOCTL_ZERO_QUERY_BYTES_COUNT CTL_CODE(ZERO_DEVICE, 0x800, METHOD_NEITHER, FILE_ANY_ACCESS)

struct ReadWriteBytesCount {
	long long readBytes;
	long long writeBytes;
};

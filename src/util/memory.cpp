#include "util/memory.h"
#include <Windows.h>

void *rwx_byte::operator new[](size_t size)
{
	return VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
}

void rwx_byte::operator delete[](void *ptr)
{
	if (ptr != nullptr)
		VirtualFree(ptr, 0, MEM_RELEASE);
}

void patch_code(void *target, const void *patch, size_t size)
{
	DWORD old_protect;
	VirtualProtect(target, size, PAGE_EXECUTE_READWRITE, &old_protect);
	memcpy(target, patch, size);
	VirtualProtect(target, size, old_protect, &old_protect);
}

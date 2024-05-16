#pragma once

#include "util/platform.h"
#include <bit>
#include <cstddef>
#include <cstdint>
#include <utility>

struct alignas(std::byte) rwx_byte {
	std::byte value;
	void *operator new[](size_t size);
	void operator delete[](void *ptr);
};

void patch_code(void *target, const void *patch, size_t size);

template<size_t N>
void patch_code(void *target, const char (&patch)[N])
{
	patch_code(target, patch, N - 1);
}

void patch_code(uintptr_t target, auto &&...args)
{
	patch_code((void*)target, std::forward<decltype(args)>(args)...);
}

void patch_vtable(void *target, size_t index, const void *hook);

void patch_vtable(uintptr_t target, auto &&...args)
{
	patch_vtable((void*)target, std::forward<decltype(args)>(args)...);
}

inline int32_t make_rel32(auto from, auto to, size_t instruction_size = 5)
{
	return (int32_t)((std::byte*)to - ((std::byte*)from + instruction_size));
}

inline void *read_rel32(auto address, size_t instruction_size = 5)
{
	const auto rel32 = *(int32_t*)((std::byte*)address + 1);
	return (std::byte*)address + rel32 + instruction_size;
}

namespace detail {
PACKED (struct op8_imm32 {
	uint8_t op;
	int32_t imm;
});
}

inline void write_call(void *address, const void *dest)
{
	*(detail::op8_imm32*)address = {0xE8, make_rel32(address, dest)};
}

inline void write_jmp(void *address, const void *dest)
{
	*(detail::op8_imm32*)address = {0xE9, make_rel32(address, dest)};
}

inline void write_push(void *address, auto value)
{
	*(detail::op8_imm32*)address = {0x68, std::bit_cast<int32_t>(value)};
}

namespace detail {

template<size_t Index, typename T>
struct call_virtual_impl;

template<size_t Index, typename ReturnType, typename ThisType, typename ...ArgTypes>
struct call_virtual_impl<Index, ReturnType(ThisType::*)(ArgTypes...)> {
	static ReturnType call(ThisType *object, ArgTypes ...args)
	{
		using func_t = ReturnType(__thiscall*)(ThisType*, ArgTypes...);
		auto **vtable = *(func_t**)object;
		return vtable[Index](object, std::forward<ArgTypes>(args)...);
	}
};

template<size_t Index, typename ReturnType, typename ThisType, typename ...ArgTypes>
struct call_virtual_impl<Index, ReturnType(ThisType::*)(ArgTypes...) const> {
	static ReturnType call(const ThisType *object, ArgTypes ...args)
	{
		using func_t = ReturnType(__thiscall*)(const ThisType*, ArgTypes...);
		auto **vtable = *(func_t**)object;
		return vtable[Index](object, std::forward<ArgTypes>(args)...);
	}
};

} // namespace detail

template<size_t Index, typename T>
inline auto call_virtual(auto *object, auto &&...args)
{
	return detail::call_virtual_impl<Index, T>::call(
		object, std::forward<decltype(args)>(args)...);
}

namespace detail::hook {
inline thread_local void *original;
}

inline uintptr_t HookGetOriginal()
{
	return (uintptr_t)detail::hook::original;
}

void patch_call_rel32(const uintptr_t address, const void *hook);

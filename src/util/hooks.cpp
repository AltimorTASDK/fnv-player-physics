#include "util/hooks.h"
#include "util/memory.h"
#include "util/platform.h"
#include <udis86.h>
#include <cstddef>
#include <memory>
#include <Windows.h>

static ud_t *get_udis86()
{
	static thread_local auto ud = [] {
		ud_t ud;
		ud_init(&ud);
		ud_set_mode(&ud, 32);
		return ud;
	}();
	return &ud;
}

static size_t get_original_code(
	const void *address,
	size_t minimum_size = 1,
	void *buffer = nullptr,
	size_t buffer_size = -1)
{
	auto *ud = get_udis86();
	ud_set_pc(ud, (uint64_t)address);
	ud_set_input_buffer(ud, (uint8_t*)address, -1);

	size_t size = 0;
	while (size < minimum_size)
		size += ud_disassemble(ud);

	if (buffer != nullptr)
		memcpy(buffer, address, std::min(size, buffer_size));

	return size;
}

detail::JmpHookImpl::JmpHookImpl(std::byte *target, const void *hook) :
	target(target),
	original(std::make_unique<rwx_byte[]>(PAGE_SIZE))
{
	PACKED (struct JmpInstruction {
		uint8_t op = 0xE9;
		int32_t rva;

		JmpInstruction(const void *from, const void *to) :
			rva(make_rel32(from, to, sizeof(JmpInstruction)))
		{
		}
	});

	constexpr auto JMP_SIZE = sizeof(JmpInstruction);
	constexpr auto FOOTER_SIZE = JMP_SIZE;
	constexpr auto COPY_SIZE = PAGE_SIZE - FOOTER_SIZE;

	const auto size = get_original_code(target, JMP_SIZE, original.get(), COPY_SIZE);

	// jmp to original after clobbered instructions
	const auto jmpStub = JmpInstruction(original.get() + size, target + size);
	memcpy(original.get() + size, &jmpStub, JMP_SIZE);

	const auto jmpHook = JmpInstruction(target, hook);
	patch_code(target, &jmpHook, JMP_SIZE);
}
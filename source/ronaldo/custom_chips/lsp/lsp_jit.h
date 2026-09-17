#pragma once

#if defined(RONALDO_NO_JIT)
// No backend. LSPDispatcher keeps m_canJit false and runs LSPInterpreter only.
#elif defined(_M_X64) || defined(__x86_64__) || defined(__x86_64) || defined(__amd__64__)
#	include "lsp_jit_x86.h"
#elif defined(__aarch64__) || defined(__ARM_ARCH_8) || defined(_M_ARM64)
#	include "lsp_jit_arm64.h"
#else
#	error "Unsupported architecture for JIT"
#endif

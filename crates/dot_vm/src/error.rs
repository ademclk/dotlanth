#![forbid(unsafe_code)]

use crate::Reg;
use dot_ops::{HostError, SyscallId};

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum VmError {
    Halted,
    InstructionPointerOutOfBounds {
        ip: usize,
        program_len: usize,
    },
    RegisterOutOfBounds {
        reg: Reg,
        register_count: usize,
    },
    SyscallWithoutHost {
        id: SyscallId,
    },
    SyscallFailed {
        id: SyscallId,
        error: HostError,
    },
    SyscallResultArityMismatch {
        id: SyscallId,
        expected: usize,
        got: usize,
    },
}

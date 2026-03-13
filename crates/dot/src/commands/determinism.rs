#![forbid(unsafe_code)]

use dot_ops::{SYSCALL_LOG_EMIT, SYSCALL_NET_HTTP_SERVE, SyscallId};
use dot_rt::DeterminismMode;
use dot_vm::Instruction;

pub(crate) fn enforce_program_supported(
    mode: DeterminismMode,
    program: &[Instruction],
) -> Result<(), String> {
    if mode != DeterminismMode::Strict {
        return Ok(());
    }

    let Some(syscall_id) = program.iter().find_map(|instruction| match instruction {
        Instruction::Syscall { id, .. } => Some(*id),
        _ => None,
    }) else {
        return Ok(());
    };

    Err(format!(
        "strict deterministic mode denied syscall `{}` before execution; opcode classification is not available until M2-OPC",
        syscall_name(syscall_id)
    ))
}

fn syscall_name(id: SyscallId) -> &'static str {
    match id {
        SYSCALL_LOG_EMIT => "log.emit",
        SYSCALL_NET_HTTP_SERVE => "net.http.serve",
        _ => "unknown",
    }
}

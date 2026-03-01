#![forbid(unsafe_code)]

use crate::{Reg, Value};
use dot_ops::SyscallId;

#[derive(Clone, Debug, PartialEq)]
pub enum Instruction {
    Halt,
    LoadConst {
        dst: Reg,
        value: Value,
    },
    Mov {
        dst: Reg,
        src: Reg,
    },
    Syscall {
        id: SyscallId,
        args: Vec<Reg>,
        results: Vec<Reg>,
    },
}

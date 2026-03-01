#![forbid(unsafe_code)]

use crate::Value;
use dot_ops::{HostError, SyscallId};

#[derive(Clone, Debug, PartialEq)]
pub struct SyscallEvent {
    pub id: SyscallId,
    pub args: Vec<Value>,
    pub result: Result<Vec<Value>, HostError>,
}

#[derive(Clone, Debug, PartialEq)]
pub enum VmEvent {
    Syscall(SyscallEvent),
}

pub trait EventSink {
    fn emit(&mut self, event: VmEvent);
}

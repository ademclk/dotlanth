#![forbid(unsafe_code)]

use crate::Value;
use dot_ops::{HostError, RuntimeEvent, SourceRef, SyscallId};

#[derive(Clone, Debug, PartialEq)]
pub struct SyscallAttemptEvent {
    pub ip: usize,
    pub id: SyscallId,
    pub args: Vec<Value>,
    pub source: Option<SourceRef>,
}

#[derive(Clone, Debug, PartialEq)]
pub struct SyscallResultEvent {
    pub ip: usize,
    pub id: SyscallId,
    pub result: Result<Vec<Value>, HostError>,
    pub source: Option<SourceRef>,
}

#[derive(Clone, Debug, PartialEq)]
pub enum VmEvent {
    SyscallAttempt(SyscallAttemptEvent),
    Runtime(RuntimeEvent),
    SyscallResult(SyscallResultEvent),
}

pub trait EventSink {
    fn emit(&mut self, event: VmEvent);
}

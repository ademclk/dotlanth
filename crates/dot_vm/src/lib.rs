#![forbid(unsafe_code)]

mod error;
mod event;
mod instruction;
mod registers;
mod value;
mod vm;

pub use error::VmError;
pub use event::{EventSink, SyscallAttemptEvent, SyscallResultEvent, VmEvent};
pub use instruction::Instruction;
pub use registers::{Reg, RegisterFile};
pub use value::Value;
pub use vm::{DEFAULT_REGISTER_COUNT, StepOutcome, Vm};

#[cfg(test)]
mod tests {
    use super::{
        EventSink, Instruction, Reg, StepOutcome, SyscallAttemptEvent, SyscallResultEvent, Value,
        Vm, VmError, VmEvent,
    };
    use dot_ops::{Host, HostError, RuntimeEvent, SyscallId};

    #[test]
    fn step_executes_loadconst_and_advances_ip() {
        let mut vm = Vm::with_register_count(
            vec![
                Instruction::LoadConst {
                    dst: Reg(0),
                    value: Value::from(42_i64),
                },
                Instruction::Halt,
            ],
            4,
        );

        assert_eq!(vm.ip(), 0);
        assert_eq!(vm.step().unwrap(), StepOutcome::Continued);
        assert_eq!(vm.ip(), 1);
        assert_eq!(vm.read(Reg(0)).unwrap(), &Value::from(42_i64));
        assert!(!vm.is_halted());
    }

    #[test]
    fn step_halts_and_future_steps_error() {
        let mut vm = Vm::with_register_count(vec![Instruction::Halt], 1);

        assert_eq!(vm.step().unwrap(), StepOutcome::Halted);
        assert!(vm.is_halted());
        assert_eq!(vm.step().unwrap_err(), VmError::Halted);
    }

    #[test]
    fn run_executes_to_completion() {
        let mut vm = Vm::with_register_count(
            vec![
                Instruction::LoadConst {
                    dst: Reg(0),
                    value: Value::from("hello"),
                },
                Instruction::Mov {
                    dst: Reg(1),
                    src: Reg(0),
                },
                Instruction::Halt,
            ],
            2,
        );

        vm.run().unwrap();
        assert_eq!(vm.read(Reg(1)).unwrap(), &Value::from("hello"));
        assert!(vm.is_halted());
    }

    #[test]
    fn syscall_invokes_host_and_writes_results() {
        struct MockHost;

        impl Host<Value> for MockHost {
            fn syscall(&mut self, id: SyscallId, args: &[Value]) -> Result<Vec<Value>, HostError> {
                assert_eq!(id, SyscallId(7));
                assert_eq!(args, &[Value::from(2_i64), Value::from(40_i64)]);
                Ok(vec![Value::from(42_i64)])
            }
        }

        let mut vm = Vm::with_register_count(
            vec![
                Instruction::LoadConst {
                    dst: Reg(0),
                    value: Value::from(2_i64),
                },
                Instruction::LoadConst {
                    dst: Reg(1),
                    value: Value::from(40_i64),
                },
                Instruction::Syscall {
                    id: SyscallId(7),
                    args: vec![Reg(0), Reg(1)],
                    results: vec![Reg(2)],
                    source: None,
                },
                Instruction::Halt,
            ],
            3,
        );

        let mut host = MockHost;
        vm.run_with_host(&mut host).unwrap();
        assert_eq!(vm.read(Reg(2)).unwrap(), &Value::from(42_i64));
    }

    #[test]
    fn syscall_errors_propagate() {
        struct ErrorHost;

        impl Host<Value> for ErrorHost {
            fn syscall(&mut self, id: SyscallId, _args: &[Value]) -> Result<Vec<Value>, HostError> {
                Err(HostError::new(format!("denied: {}", id.0)))
            }
        }

        let mut vm = Vm::with_register_count(
            vec![Instruction::Syscall {
                id: SyscallId(1),
                args: vec![],
                results: vec![],
                source: None,
            }],
            1,
        );

        let mut host = ErrorHost;
        assert_eq!(
            vm.run_with_host(&mut host).unwrap_err(),
            VmError::SyscallFailed {
                id: SyscallId(1),
                error: HostError::new("denied: 1"),
            }
        );
    }

    #[test]
    fn syscall_emits_events_for_recording() {
        struct MockHost;

        impl Host<Value> for MockHost {
            fn syscall(
                &mut self,
                _id: SyscallId,
                _args: &[Value],
            ) -> Result<Vec<Value>, HostError> {
                Ok(vec![])
            }
        }

        #[derive(Default)]
        struct CollectSink {
            events: Vec<VmEvent>,
        }

        impl EventSink for CollectSink {
            fn emit(&mut self, event: VmEvent) {
                self.events.push(event);
            }
        }

        let mut vm = Vm::with_register_count(
            vec![
                Instruction::LoadConst {
                    dst: Reg(0),
                    value: Value::from("hi"),
                },
                Instruction::Syscall {
                    id: SyscallId(9),
                    args: vec![Reg(0)],
                    results: vec![],
                    source: None,
                },
                Instruction::Halt,
            ],
            1,
        );

        let mut host = MockHost;
        let mut sink = CollectSink::default();
        vm.run_with_host_and_events(&mut host, &mut sink).unwrap();

        assert_eq!(
            sink.events,
            vec![
                VmEvent::SyscallAttempt(SyscallAttemptEvent {
                    ip: 1,
                    id: SyscallId(9),
                    args: vec![Value::from("hi")],
                    source: None,
                }),
                VmEvent::SyscallResult(SyscallResultEvent {
                    ip: 1,
                    id: SyscallId(9),
                    result: Ok(vec![]),
                    source: None,
                }),
            ]
        );
    }

    #[test]
    fn syscall_error_emits_event_before_failing() {
        struct ErrorHost;

        impl Host<Value> for ErrorHost {
            fn syscall(
                &mut self,
                _id: SyscallId,
                _args: &[Value],
            ) -> Result<Vec<Value>, HostError> {
                Err(HostError::new("boom"))
            }
        }

        struct CollectSink(Vec<VmEvent>);

        impl EventSink for CollectSink {
            fn emit(&mut self, event: VmEvent) {
                self.0.push(event);
            }
        }

        let mut vm = Vm::with_register_count(
            vec![Instruction::Syscall {
                id: SyscallId(2),
                args: vec![],
                results: vec![],
                source: None,
            }],
            1,
        );

        let mut host = ErrorHost;
        let mut sink = CollectSink(vec![]);
        assert_eq!(
            vm.run_with_host_and_events(&mut host, &mut sink)
                .unwrap_err(),
            VmError::SyscallFailed {
                id: SyscallId(2),
                error: HostError::new("boom"),
            }
        );
        assert_eq!(
            sink.0,
            vec![
                VmEvent::SyscallAttempt(SyscallAttemptEvent {
                    ip: 0,
                    id: SyscallId(2),
                    args: vec![],
                    source: None,
                }),
                VmEvent::SyscallResult(SyscallResultEvent {
                    ip: 0,
                    id: SyscallId(2),
                    result: Err(HostError::new("boom")),
                    source: None,
                }),
            ]
        );
    }

    #[test]
    fn runtime_events_are_emitted_between_syscall_attempt_and_result() {
        struct TraceHost {
            pending: Vec<RuntimeEvent>,
        }

        impl Host<Value> for TraceHost {
            fn syscall(
                &mut self,
                _id: SyscallId,
                _args: &[Value],
            ) -> Result<Vec<Value>, HostError> {
                self.pending.push(RuntimeEvent::Log {
                    message: "during-call".to_owned(),
                    source: None,
                });
                Ok(vec![])
            }

            fn take_runtime_events(&mut self) -> Vec<RuntimeEvent> {
                std::mem::take(&mut self.pending)
            }
        }

        struct CollectSink(Vec<VmEvent>);

        impl EventSink for CollectSink {
            fn emit(&mut self, event: VmEvent) {
                self.0.push(event);
            }
        }

        let mut vm = Vm::with_register_count(
            vec![
                Instruction::Syscall {
                    id: SyscallId(3),
                    args: vec![],
                    results: vec![],
                    source: None,
                },
                Instruction::Halt,
            ],
            1,
        );

        let mut host = TraceHost {
            pending: Vec::new(),
        };
        let mut sink = CollectSink(Vec::new());
        vm.run_with_host_and_events(&mut host, &mut sink)
            .expect("vm should succeed");

        assert_eq!(
            sink.0,
            vec![
                VmEvent::SyscallAttempt(SyscallAttemptEvent {
                    ip: 0,
                    id: SyscallId(3),
                    args: vec![],
                    source: None,
                }),
                VmEvent::Runtime(RuntimeEvent::Log {
                    message: "during-call".to_owned(),
                    source: None,
                }),
                VmEvent::SyscallResult(SyscallResultEvent {
                    ip: 0,
                    id: SyscallId(3),
                    result: Ok(vec![]),
                    source: None,
                }),
            ]
        );
    }
}

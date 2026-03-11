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
    use dot_ops::{
        Host, HostError, RuntimeEvent, SYSCALL_LOG_EMIT, SYSCALL_NET_HTTP_SERVE, SourceRef,
        SourceSpan, SyscallId,
    };
    use dot_sec::{Capability, CapabilitySet};
    use std::future::Future;
    use std::pin::Pin;

    fn block_on<F: Future>(future: F) -> F::Output {
        tokio::runtime::Builder::new_current_thread()
            .enable_all()
            .build()
            .expect("runtime must build")
            .block_on(future)
    }

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
            fn syscall<'a>(
                &'a mut self,
                id: SyscallId,
                args: &'a [Value],
            ) -> Pin<Box<dyn Future<Output = Result<Vec<Value>, HostError>> + 'a>> {
                Box::pin(async move {
                    assert_eq!(id, SyscallId(7));
                    assert_eq!(args, &[Value::from(2_i64), Value::from(40_i64)]);
                    Ok(vec![Value::from(42_i64)])
                })
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
        block_on(vm.run_with_host(&mut host)).unwrap();
        assert_eq!(vm.read(Reg(2)).unwrap(), &Value::from(42_i64));
    }

    #[test]
    fn syscall_errors_propagate() {
        struct ErrorHost;

        impl Host<Value> for ErrorHost {
            fn syscall<'a>(
                &'a mut self,
                id: SyscallId,
                _args: &'a [Value],
            ) -> Pin<Box<dyn Future<Output = Result<Vec<Value>, HostError>> + 'a>> {
                Box::pin(async move { Err(HostError::new(format!("denied: {}", id.0))) })
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
            block_on(vm.run_with_host(&mut host)).unwrap_err(),
            VmError::SyscallFailed {
                id: SyscallId(1),
                error: HostError::new("denied: 1"),
            }
        );
    }

    #[test]
    fn capability_denial_happens_before_host_dispatch() {
        struct CountingHost {
            calls: usize,
        }

        impl Host<Value> for CountingHost {
            fn syscall<'a>(
                &'a mut self,
                _id: SyscallId,
                _args: &'a [Value],
            ) -> Pin<Box<dyn Future<Output = Result<Vec<Value>, HostError>> + 'a>> {
                self.calls += 1;
                Box::pin(async { Ok(vec![]) })
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
        let capabilities = CapabilitySet::empty();
        let mut host = CountingHost { calls: 0 };

        let error = block_on(vm.run_with_capabilities_and_host(&capabilities, &mut host))
            .expect_err("missing capability must fail before host dispatch");

        assert_eq!(
            error,
            VmError::SyscallFailed {
                id: SyscallId(1),
                error: HostError::new(
                    "capability denied: syscall `log.emit` requires capability `log`. Hint: add `allow log`. Declare it in your `.dot` file with an `allow ...` statement."
                ),
            }
        );
        assert_eq!(host.calls, 0);
    }

    #[test]
    fn capability_denial_emits_attempt_and_result_events_before_host_dispatch() {
        struct CountingHost {
            calls: usize,
        }

        impl Host<Value> for CountingHost {
            fn syscall<'a>(
                &'a mut self,
                _id: SyscallId,
                _args: &'a [Value],
            ) -> Pin<Box<dyn Future<Output = Result<Vec<Value>, HostError>> + 'a>> {
                self.calls += 1;
                Box::pin(async { Ok(vec![]) })
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
            vec![Instruction::Syscall {
                id: SYSCALL_LOG_EMIT,
                args: vec![],
                results: vec![],
                source: None,
            }],
            1,
        );
        let capabilities = CapabilitySet::empty();
        let mut host = CountingHost { calls: 0 };
        let mut sink = CollectSink::default();

        let error =
            block_on(vm.run_with_capabilities_host_and_events(&capabilities, &mut host, &mut sink))
                .expect_err("missing capability must fail before host dispatch");

        assert_eq!(
            error,
            VmError::SyscallFailed {
                id: SYSCALL_LOG_EMIT,
                error: HostError::new(
                    "capability denied: syscall `log.emit` requires capability `log`. Hint: add `allow log`. Declare it in your `.dot` file with an `allow ...` statement."
                ),
            }
        );
        assert_eq!(host.calls, 0);
        assert_eq!(
            sink.events,
            vec![
                VmEvent::SyscallAttempt(SyscallAttemptEvent {
                    ip: 0,
                    id: SYSCALL_LOG_EMIT,
                    args: vec![],
                    source: None,
                }),
                VmEvent::SyscallResult(SyscallResultEvent {
                    ip: 0,
                    id: SYSCALL_LOG_EMIT,
                    result: Err(HostError::new(
                        "capability denied: syscall `log.emit` requires capability `log`. Hint: add `allow log`. Declare it in your `.dot` file with an `allow ...` statement."
                    )),
                    source: None,
                }),
            ]
        );
    }

    #[test]
    fn granted_capability_dispatches_host_and_writes_results() {
        struct LogHost {
            calls: usize,
        }

        impl Host<Value> for LogHost {
            fn syscall<'a>(
                &'a mut self,
                id: SyscallId,
                args: &'a [Value],
            ) -> Pin<Box<dyn Future<Output = Result<Vec<Value>, HostError>> + 'a>> {
                self.calls += 1;
                Box::pin(async move {
                    assert_eq!(id, SYSCALL_LOG_EMIT);
                    assert_eq!(args, &[Value::from("hello")]);
                    Ok(vec![Value::from("logged")])
                })
            }
        }

        let mut vm = Vm::with_register_count(
            vec![
                Instruction::LoadConst {
                    dst: Reg(0),
                    value: Value::from("hello"),
                },
                Instruction::Syscall {
                    id: SYSCALL_LOG_EMIT,
                    args: vec![Reg(0)],
                    results: vec![Reg(1)],
                    source: None,
                },
                Instruction::Halt,
            ],
            2,
        );
        let mut capabilities = CapabilitySet::empty();
        capabilities.insert(Capability::Log);
        let mut host = LogHost { calls: 0 };

        block_on(vm.run_with_capabilities_and_host(&capabilities, &mut host))
            .expect("granted capability must dispatch host");

        assert_eq!(host.calls, 1);
        assert_eq!(vm.read(Reg(1)).unwrap(), &Value::from("logged"));
        assert!(vm.is_halted());
        assert_eq!(vm.ip(), 3);
    }

    #[test]
    fn ungated_syscalls_bypass_capability_checks() {
        struct MockHost {
            calls: usize,
        }

        impl Host<Value> for MockHost {
            fn syscall<'a>(
                &'a mut self,
                id: SyscallId,
                args: &'a [Value],
            ) -> Pin<Box<dyn Future<Output = Result<Vec<Value>, HostError>> + 'a>> {
                self.calls += 1;
                Box::pin(async move {
                    assert_eq!(id, SyscallId(99));
                    assert_eq!(args, &[Value::from(2_i64), Value::from(40_i64)]);
                    Ok(vec![Value::from(42_i64)])
                })
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
                    id: SyscallId(99),
                    args: vec![Reg(0), Reg(1)],
                    results: vec![Reg(2)],
                    source: None,
                },
                Instruction::Halt,
            ],
            3,
        );
        let mut host = MockHost { calls: 0 };

        block_on(vm.run_with_capabilities_and_host(&CapabilitySet::empty(), &mut host))
            .expect("ungated syscalls must bypass capability checks");

        assert_eq!(host.calls, 1);
        assert_eq!(vm.read(Reg(2)).unwrap(), &Value::from(42_i64));
    }

    #[test]
    fn missing_http_listen_capability_denies_http_serve_syscall() {
        struct CountingHost {
            calls: usize,
        }

        impl Host<Value> for CountingHost {
            fn syscall<'a>(
                &'a mut self,
                _id: SyscallId,
                _args: &'a [Value],
            ) -> Pin<Box<dyn Future<Output = Result<Vec<Value>, HostError>> + 'a>> {
                self.calls += 1;
                Box::pin(async { Ok(vec![]) })
            }
        }

        let mut vm = Vm::with_register_count(
            vec![Instruction::Syscall {
                id: SYSCALL_NET_HTTP_SERVE,
                args: vec![],
                results: vec![],
                source: None,
            }],
            1,
        );
        let mut host = CountingHost { calls: 0 };

        let error = block_on(vm.run_with_capabilities_and_host(&CapabilitySet::empty(), &mut host))
            .expect_err("missing net.http.listen capability must fail");

        assert_eq!(
            error,
            VmError::SyscallFailed {
                id: SYSCALL_NET_HTTP_SERVE,
                error: HostError::new(
                    "capability denied: syscall `net.http.serve` requires capability `net.http.listen`. Hint: add `allow net.http.listen`. Declare it in your `.dot` file with an `allow ...` statement."
                ),
            }
        );
        assert_eq!(host.calls, 0);
    }

    #[test]
    fn denied_capability_step_leaves_vm_state_unchanged() {
        struct CountingHost {
            calls: usize,
        }

        impl Host<Value> for CountingHost {
            fn syscall<'a>(
                &'a mut self,
                _id: SyscallId,
                _args: &'a [Value],
            ) -> Pin<Box<dyn Future<Output = Result<Vec<Value>, HostError>> + 'a>> {
                self.calls += 1;
                Box::pin(async { Ok(vec![]) })
            }
        }

        let mut vm = Vm::with_register_count(
            vec![
                Instruction::LoadConst {
                    dst: Reg(0),
                    value: Value::from("keep"),
                },
                Instruction::Syscall {
                    id: SYSCALL_LOG_EMIT,
                    args: vec![Reg(0)],
                    results: vec![],
                    source: None,
                },
            ],
            1,
        );
        assert_eq!(vm.step().unwrap(), StepOutcome::Continued);
        let mut host = CountingHost { calls: 0 };

        let error =
            block_on(vm.step_with_capabilities_and_host(&CapabilitySet::empty(), &mut host))
                .expect_err("denied syscall must fail");

        assert_eq!(
            error,
            VmError::SyscallFailed {
                id: SYSCALL_LOG_EMIT,
                error: HostError::new(
                    "capability denied: syscall `log.emit` requires capability `log`. Hint: add `allow log`. Declare it in your `.dot` file with an `allow ...` statement."
                ),
            }
        );
        assert_eq!(host.calls, 0);
        assert_eq!(vm.ip(), 1);
        assert!(!vm.is_halted());
        assert_eq!(vm.read(Reg(0)).unwrap(), &Value::from("keep"));
    }

    #[test]
    fn granted_capability_preserves_event_order_and_runtime_events() {
        struct TraceHost {
            pending: Vec<RuntimeEvent>,
        }

        impl Host<Value> for TraceHost {
            fn syscall<'a>(
                &'a mut self,
                id: SyscallId,
                args: &'a [Value],
            ) -> Pin<Box<dyn Future<Output = Result<Vec<Value>, HostError>> + 'a>> {
                Box::pin(async move {
                    assert_eq!(id, SYSCALL_LOG_EMIT);
                    assert_eq!(args, &[Value::from("hi")]);
                    self.pending.push(RuntimeEvent::Log {
                        message: "during-capability-call".to_owned(),
                        source: None,
                    });
                    Ok(vec![])
                })
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
                Instruction::LoadConst {
                    dst: Reg(0),
                    value: Value::from("hi"),
                },
                Instruction::Syscall {
                    id: SYSCALL_LOG_EMIT,
                    args: vec![Reg(0)],
                    results: vec![],
                    source: None,
                },
                Instruction::Halt,
            ],
            1,
        );
        let mut capabilities = CapabilitySet::empty();
        capabilities.insert(Capability::Log);
        let mut host = TraceHost {
            pending: Vec::new(),
        };
        let mut sink = CollectSink(Vec::new());

        block_on(vm.run_with_capabilities_host_and_events(&capabilities, &mut host, &mut sink))
            .expect("granted capability path must succeed");

        assert_eq!(
            sink.0,
            vec![
                VmEvent::SyscallAttempt(SyscallAttemptEvent {
                    ip: 1,
                    id: SYSCALL_LOG_EMIT,
                    args: vec![Value::from("hi")],
                    source: None,
                }),
                VmEvent::Runtime(RuntimeEvent::Log {
                    message: "during-capability-call".to_owned(),
                    source: None,
                }),
                VmEvent::SyscallResult(SyscallResultEvent {
                    ip: 1,
                    id: SYSCALL_LOG_EMIT,
                    result: Ok(vec![]),
                    source: None,
                }),
            ]
        );
    }

    #[test]
    fn denied_capability_events_preserve_source_metadata() {
        struct CountingHost;

        impl Host<Value> for CountingHost {
            fn syscall<'a>(
                &'a mut self,
                _id: SyscallId,
                _args: &'a [Value],
            ) -> Pin<Box<dyn Future<Output = Result<Vec<Value>, HostError>> + 'a>> {
                Box::pin(async { Ok(vec![]) })
            }
        }

        struct CollectSink(Vec<VmEvent>);

        impl EventSink for CollectSink {
            fn emit(&mut self, event: VmEvent) {
                self.0.push(event);
            }
        }

        let source = Some(SourceRef::with_span_and_path(
            SourceSpan::new(4, 3, 8),
            "server",
        ));
        let mut vm = Vm::with_register_count(
            vec![Instruction::Syscall {
                id: SYSCALL_NET_HTTP_SERVE,
                args: vec![],
                results: vec![],
                source: source.clone(),
            }],
            1,
        );
        let mut host = CountingHost;
        let mut sink = CollectSink(Vec::new());

        let error = block_on(vm.run_with_capabilities_host_and_events(
            &CapabilitySet::empty(),
            &mut host,
            &mut sink,
        ))
        .expect_err("missing capability must fail");

        assert_eq!(
            error,
            VmError::SyscallFailed {
                id: SYSCALL_NET_HTTP_SERVE,
                error: HostError::new(
                    "capability denied: syscall `net.http.serve` requires capability `net.http.listen`. Hint: add `allow net.http.listen`. Declare it in your `.dot` file with an `allow ...` statement."
                ),
            }
        );
        assert_eq!(
            sink.0,
            vec![
                VmEvent::SyscallAttempt(SyscallAttemptEvent {
                    ip: 0,
                    id: SYSCALL_NET_HTTP_SERVE,
                    args: vec![],
                    source: source.clone(),
                }),
                VmEvent::SyscallResult(SyscallResultEvent {
                    ip: 0,
                    id: SYSCALL_NET_HTTP_SERVE,
                    result: Err(HostError::new(
                        "capability denied: syscall `net.http.serve` requires capability `net.http.listen`. Hint: add `allow net.http.listen`. Declare it in your `.dot` file with an `allow ...` statement."
                    )),
                    source,
                }),
            ]
        );
    }

    #[test]
    fn syscall_emits_events_for_recording() {
        struct MockHost;

        impl Host<Value> for MockHost {
            fn syscall<'a>(
                &'a mut self,
                _id: SyscallId,
                _args: &'a [Value],
            ) -> Pin<Box<dyn Future<Output = Result<Vec<Value>, HostError>> + 'a>> {
                Box::pin(async { Ok(vec![]) })
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
        block_on(vm.run_with_host_and_events(&mut host, &mut sink)).unwrap();

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
            fn syscall<'a>(
                &'a mut self,
                _id: SyscallId,
                _args: &'a [Value],
            ) -> Pin<Box<dyn Future<Output = Result<Vec<Value>, HostError>> + 'a>> {
                Box::pin(async { Err(HostError::new("boom")) })
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
            block_on(vm.run_with_host_and_events(&mut host, &mut sink)).unwrap_err(),
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
            fn syscall<'a>(
                &'a mut self,
                _id: SyscallId,
                _args: &'a [Value],
            ) -> Pin<Box<dyn Future<Output = Result<Vec<Value>, HostError>> + 'a>> {
                Box::pin(async move {
                    self.pending.push(RuntimeEvent::Log {
                        message: "during-call".to_owned(),
                        source: None,
                    });
                    Ok(vec![])
                })
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
        block_on(vm.run_with_host_and_events(&mut host, &mut sink)).expect("vm should succeed");

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

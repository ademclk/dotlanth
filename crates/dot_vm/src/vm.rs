#![forbid(unsafe_code)]

use crate::{
    EventSink, Instruction, Reg, RegisterFile, SyscallAttemptEvent, SyscallResultEvent, Value,
    VmError, VmEvent,
};
use dot_ops::{Host, SyscallId, syscall_spec};
use dot_sec::{CapabilitySet, Syscall};

pub const DEFAULT_REGISTER_COUNT: usize = 32;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum StepOutcome {
    Continued,
    Halted,
}

#[derive(Clone, Debug, PartialEq)]
pub struct Vm {
    program: Vec<Instruction>,
    registers: RegisterFile,
    ip: usize,
    halted: bool,
}

impl Vm {
    pub fn new(program: Vec<Instruction>) -> Self {
        Self::with_register_count(program, DEFAULT_REGISTER_COUNT)
    }

    pub fn with_register_count(program: Vec<Instruction>, register_count: usize) -> Self {
        Self {
            program,
            registers: RegisterFile::new(register_count),
            ip: 0,
            halted: false,
        }
    }

    pub fn ip(&self) -> usize {
        self.ip
    }

    pub fn is_halted(&self) -> bool {
        self.halted
    }

    pub fn registers(&self) -> &RegisterFile {
        &self.registers
    }

    pub fn registers_mut(&mut self) -> &mut RegisterFile {
        &mut self.registers
    }

    pub fn read(&self, reg: Reg) -> Result<&Value, VmError> {
        self.registers.get(reg)
    }

    pub fn write(&mut self, reg: Reg, value: Value) -> Result<(), VmError> {
        self.registers.set(reg, value)
    }

    pub fn step(&mut self) -> Result<StepOutcome, VmError> {
        self.step_inner()
    }

    pub async fn step_with_host(
        &mut self,
        host: &mut dyn Host<Value>,
    ) -> Result<StepOutcome, VmError> {
        self.step_inner_async(None, host, None).await
    }

    pub async fn step_with_host_and_events(
        &mut self,
        host: &mut dyn Host<Value>,
        events: &mut dyn EventSink,
    ) -> Result<StepOutcome, VmError> {
        self.step_inner_async(None, host, Some(events)).await
    }

    pub async fn step_with_capabilities_and_host(
        &mut self,
        capabilities: &CapabilitySet,
        host: &mut dyn Host<Value>,
    ) -> Result<StepOutcome, VmError> {
        self.step_inner_async(Some(capabilities), host, None).await
    }

    pub async fn step_with_capabilities_host_and_events(
        &mut self,
        capabilities: &CapabilitySet,
        host: &mut dyn Host<Value>,
        events: &mut dyn EventSink,
    ) -> Result<StepOutcome, VmError> {
        self.step_inner_async(Some(capabilities), host, Some(events))
            .await
    }

    fn step_inner(&mut self) -> Result<StepOutcome, VmError> {
        if self.halted {
            return Err(VmError::Halted);
        }

        let instruction = self
            .program
            .get(self.ip)
            .ok_or(VmError::InstructionPointerOutOfBounds {
                ip: self.ip,
                program_len: self.program.len(),
            })?
            .clone();

        match instruction {
            Instruction::Halt => {
                self.ip += 1;
                self.halted = true;
                Ok(StepOutcome::Halted)
            }
            Instruction::LoadConst { dst, value } => {
                self.write(dst, value)?;
                self.ip += 1;
                Ok(StepOutcome::Continued)
            }
            Instruction::Mov { dst, src } => {
                let value = self.read(src)?.clone();
                self.write(dst, value)?;
                self.ip += 1;
                Ok(StepOutcome::Continued)
            }
            Instruction::Syscall { id, .. } => Err(VmError::SyscallWithoutHost { id }),
        }
    }

    async fn step_inner_async(
        &mut self,
        capabilities: Option<&CapabilitySet>,
        host: &mut dyn Host<Value>,
        mut events: Option<&mut dyn EventSink>,
    ) -> Result<StepOutcome, VmError> {
        if self.halted {
            return Err(VmError::Halted);
        }

        let instruction = self
            .program
            .get(self.ip)
            .ok_or(VmError::InstructionPointerOutOfBounds {
                ip: self.ip,
                program_len: self.program.len(),
            })?
            .clone();

        match instruction {
            Instruction::Halt => {
                self.ip += 1;
                self.halted = true;
                Ok(StepOutcome::Halted)
            }
            Instruction::LoadConst { dst, value } => {
                self.write(dst, value)?;
                self.ip += 1;
                Ok(StepOutcome::Continued)
            }
            Instruction::Mov { dst, src } => {
                let value = self.read(src)?.clone();
                self.write(dst, value)?;
                self.ip += 1;
                Ok(StepOutcome::Continued)
            }
            Instruction::Syscall {
                id,
                args,
                results,
                source,
            } => {
                let mut values = Vec::with_capacity(args.len());
                for reg in args {
                    values.push(self.read(reg)?.clone());
                }

                if let Some(events) = events.as_mut() {
                    (*events).emit(VmEvent::SyscallAttempt(SyscallAttemptEvent {
                        ip: self.ip,
                        id,
                        args: values.clone(),
                        source: source.clone(),
                    }));
                }

                if let Some(error) = capabilities
                    .and_then(|set| capability_gated_syscall(id).map(|syscall| (set, syscall)))
                    .and_then(|(set, syscall)| {
                        set.enforce(syscall)
                            .err()
                            .map(|error| dot_ops::HostError::new(error.to_string()))
                    })
                {
                    if let Some(events) = events.as_mut() {
                        (*events).emit(VmEvent::SyscallResult(SyscallResultEvent {
                            ip: self.ip,
                            id,
                            result: Err(error.clone()),
                            source,
                        }));
                    }
                    return Err(VmError::SyscallFailed { id, error });
                }

                let syscall_outcome = host.syscall(id, &values).await;
                let runtime_events = host.take_runtime_events();
                if let Some(events) = events.as_mut() {
                    for runtime_event in runtime_events {
                        (*events).emit(VmEvent::Runtime(runtime_event));
                    }
                }

                let returned = match syscall_outcome {
                    Ok(returned) => {
                        if let Some(events) = events.as_mut() {
                            (*events).emit(VmEvent::SyscallResult(SyscallResultEvent {
                                ip: self.ip,
                                id,
                                result: Ok(returned.clone()),
                                source: source.clone(),
                            }));
                        }
                        returned
                    }
                    Err(error) => {
                        if let Some(events) = events.as_mut() {
                            (*events).emit(VmEvent::SyscallResult(SyscallResultEvent {
                                ip: self.ip,
                                id,
                                result: Err(error.clone()),
                                source,
                            }));
                        }
                        return Err(VmError::SyscallFailed { id, error });
                    }
                };

                if returned.len() != results.len() {
                    return Err(VmError::SyscallResultArityMismatch {
                        id,
                        expected: results.len(),
                        got: returned.len(),
                    });
                }

                for (dst, value) in results.into_iter().zip(returned) {
                    self.write(dst, value)?;
                }

                self.ip += 1;
                Ok(StepOutcome::Continued)
            }
        }
    }

    pub fn run(&mut self) -> Result<(), VmError> {
        while !self.halted {
            self.step()?;
        }
        Ok(())
    }

    pub async fn run_with_host(&mut self, host: &mut dyn Host<Value>) -> Result<(), VmError> {
        while !self.halted {
            self.step_with_host(host).await?;
        }
        Ok(())
    }

    pub async fn run_with_host_and_events(
        &mut self,
        host: &mut dyn Host<Value>,
        events: &mut dyn EventSink,
    ) -> Result<(), VmError> {
        while !self.halted {
            self.step_inner_async(None, host, Some(&mut *events))
                .await?;
        }
        Ok(())
    }

    pub async fn run_with_capabilities_and_host(
        &mut self,
        capabilities: &CapabilitySet,
        host: &mut dyn Host<Value>,
    ) -> Result<(), VmError> {
        while !self.halted {
            self.step_with_capabilities_and_host(capabilities, host)
                .await?;
        }
        Ok(())
    }

    pub async fn run_with_capabilities_host_and_events(
        &mut self,
        capabilities: &CapabilitySet,
        host: &mut dyn Host<Value>,
        events: &mut dyn EventSink,
    ) -> Result<(), VmError> {
        while !self.halted {
            self.step_inner_async(Some(capabilities), host, Some(&mut *events))
                .await?;
        }
        Ok(())
    }
}

fn capability_gated_syscall(id: SyscallId) -> Option<Syscall> {
    syscall_spec(id).map(|spec| spec.syscall())
}

#[cfg(test)]
mod tests {
    use super::capability_gated_syscall;

    #[test]
    fn capability_gate_tracks_registered_syscalls() {
        for spec in dot_ops::registered_syscalls() {
            assert_eq!(capability_gated_syscall(spec.id()), Some(spec.syscall()));
        }
    }
}

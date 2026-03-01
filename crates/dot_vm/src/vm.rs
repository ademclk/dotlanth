#![forbid(unsafe_code)]

use crate::{EventSink, Instruction, Reg, RegisterFile, SyscallEvent, Value, VmError, VmEvent};
use dot_ops::Host;

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
        self.step_inner(None, None)
    }

    pub fn step_with_host(&mut self, host: &mut dyn Host<Value>) -> Result<StepOutcome, VmError> {
        self.step_inner(Some(host), None)
    }

    pub fn step_with_host_and_events(
        &mut self,
        host: &mut dyn Host<Value>,
        events: &mut dyn EventSink,
    ) -> Result<StepOutcome, VmError> {
        self.step_inner(Some(host), Some(events))
    }

    fn step_inner(
        &mut self,
        host: Option<&mut dyn Host<Value>>,
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
            Instruction::Syscall { id, args, results } => {
                let host = host.ok_or(VmError::SyscallWithoutHost { id })?;

                let mut values = Vec::with_capacity(args.len());
                for reg in args {
                    values.push(self.read(reg)?.clone());
                }

                let returned = match host.syscall(id, &values) {
                    Ok(returned) => {
                        if let Some(events) = events.as_mut() {
                            (*events).emit(VmEvent::Syscall(SyscallEvent {
                                id,
                                args: values,
                                result: Ok(returned.clone()),
                            }));
                        }
                        returned
                    }
                    Err(error) => {
                        if let Some(events) = events.as_mut() {
                            (*events).emit(VmEvent::Syscall(SyscallEvent {
                                id,
                                args: values,
                                result: Err(error.clone()),
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

    pub fn run_with_host(&mut self, host: &mut dyn Host<Value>) -> Result<(), VmError> {
        while !self.halted {
            self.step_with_host(host)?;
        }
        Ok(())
    }

    pub fn run_with_host_and_events(
        &mut self,
        host: &mut dyn Host<Value>,
        events: &mut dyn EventSink,
    ) -> Result<(), VmError> {
        while !self.halted {
            self.step_inner(Some(host), Some(events))?;
        }
        Ok(())
    }
}

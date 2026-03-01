#![forbid(unsafe_code)]

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct SyscallId(pub u32);

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct HostError {
    message: String,
}

impl HostError {
    pub fn new(message: impl Into<String>) -> Self {
        Self {
            message: message.into(),
        }
    }

    pub fn message(&self) -> &str {
        &self.message
    }
}

impl std::fmt::Display for HostError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(&self.message)
    }
}

impl std::error::Error for HostError {}

/// Host interface used by the VM to perform capability-gated side effects.
///
/// The VM remains deterministic: nondeterminism may only enter via this interface.
pub trait Host<V> {
    fn syscall(&mut self, id: SyscallId, args: &[V]) -> Result<Vec<V>, HostError>;
}

#[cfg(test)]
mod tests {
    use super::{Host, HostError, SyscallId};

    struct NoopHost;

    impl Host<u8> for NoopHost {
        fn syscall(&mut self, _id: SyscallId, _args: &[u8]) -> Result<Vec<u8>, HostError> {
            Ok(vec![])
        }
    }

    #[test]
    fn host_is_object_safe_over_concrete_value_types() {
        let mut host: Box<dyn Host<u8>> = Box::new(NoopHost);
        host.syscall(SyscallId(0), &[]).unwrap();
    }
}

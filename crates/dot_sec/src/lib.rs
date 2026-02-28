#![forbid(unsafe_code)]

use std::collections::BTreeSet;
use std::str::FromStr;

/// A stable identifier for a side-effect capability.
///
/// These identifiers are part of the dotDSL surface area via `allow <capability>`.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum Capability {
    /// Allows emitting logs through the host/runtime.
    Log,
    /// Allows binding an HTTP listener (e.g. `server listen 8080`).
    NetHttpListen,
}

impl Capability {
    /// Returns the stable string identifier for this capability.
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Log => "log",
            Self::NetHttpListen => "net.http.listen",
        }
    }

    /// Returns all supported capabilities for the current runtime version.
    pub const fn all() -> &'static [Capability] {
        &[Self::Log, Self::NetHttpListen]
    }
}

impl std::fmt::Display for Capability {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(self.as_str())
    }
}

impl FromStr for Capability {
    type Err = UnknownCapabilityError;

    fn from_str(value: &str) -> Result<Self, Self::Err> {
        match value {
            "log" => Ok(Self::Log),
            "net.http.listen" => Ok(Self::NetHttpListen),
            _ => Err(UnknownCapabilityError::new(value)),
        }
    }
}

/// A runtime/host syscall that produces side effects and must be capability-gated.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Syscall {
    /// Emitting a log line to the host.
    LogEmit,
    /// Binding an HTTP listener.
    NetHttpListen,
}

impl Syscall {
    /// Returns the stable syscall name used in denial errors.
    pub const fn name(self) -> &'static str {
        match self {
            Self::LogEmit => "log.emit",
            Self::NetHttpListen => "net.http.listen",
        }
    }

    /// Returns the capability required to perform this syscall.
    pub const fn required_capability(self) -> Capability {
        match self {
            Self::LogEmit => Capability::Log,
            Self::NetHttpListen => Capability::NetHttpListen,
        }
    }

    /// Returns the dotDSL `allow` statement that grants the required capability.
    pub const fn allow_statement(self) -> &'static str {
        match self {
            Self::LogEmit => "allow log",
            Self::NetHttpListen => "allow net.http.listen",
        }
    }
}

/// A deny-by-default set of granted capabilities.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct CapabilitySet {
    granted: BTreeSet<Capability>,
}

impl CapabilitySet {
    /// Creates an empty set (deny-by-default).
    pub fn empty() -> Self {
        Self::default()
    }

    /// Returns true if the set contains the given capability.
    pub fn contains(&self, capability: Capability) -> bool {
        self.granted.contains(&capability)
    }

    /// Inserts a capability.
    pub fn insert(&mut self, capability: Capability) {
        self.granted.insert(capability);
    }

    /// Enforces the capability required for the given syscall.
    ///
    /// Deny-by-default: an empty set denies all side-effect syscalls.
    pub fn enforce(&self, syscall: Syscall) -> Result<(), SyscallDeniedError> {
        let required = syscall.required_capability();
        if self.contains(required) {
            Ok(())
        } else {
            Err(SyscallDeniedError::new(syscall))
        }
    }

    /// Builds a capability set from string identifiers.
    ///
    /// Unknown identifiers fail clearly at runtime setup.
    pub fn try_from_iter<'a, I>(capabilities: I) -> Result<Self, UnknownCapabilityError>
    where
        I: IntoIterator<Item = &'a str>,
    {
        let mut set = Self::empty();
        for cap in capabilities {
            set.insert(Capability::from_str(cap)?);
        }
        Ok(set)
    }
}

/// Returned when a syscall is denied due to a missing capability.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SyscallDeniedError {
    syscall: Syscall,
}

impl SyscallDeniedError {
    fn new(syscall: Syscall) -> Self {
        Self { syscall }
    }

    /// The denied syscall.
    pub const fn syscall(&self) -> Syscall {
        self.syscall
    }

    /// The capability required for the denied syscall.
    pub const fn required_capability(&self) -> Capability {
        self.syscall.required_capability()
    }

    /// A user-facing hint describing how to grant the required capability.
    pub const fn fix_hint(&self) -> &'static str {
        "Declare it in your `.dot` file with an `allow ...` statement."
    }
}

impl std::fmt::Display for SyscallDeniedError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(
            f,
            "capability denied: syscall `{}` requires capability `{}`. Hint: add `{}`. {}",
            self.syscall.name(),
            self.required_capability(),
            self.syscall.allow_statement(),
            self.fix_hint(),
        )
    }
}

impl std::error::Error for SyscallDeniedError {}

/// Returned when attempting to parse an unknown capability identifier.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct UnknownCapabilityError {
    input: String,
}

impl UnknownCapabilityError {
    fn new(input: &str) -> Self {
        Self {
            input: input.to_owned(),
        }
    }

    /// The unknown capability identifier string.
    pub fn input(&self) -> &str {
        &self.input
    }
}

impl std::fmt::Display for UnknownCapabilityError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "unknown capability `{}`", self.input)
    }
}

impl std::error::Error for UnknownCapabilityError {}

#[cfg(test)]
mod tests {
    use super::{Capability, CapabilitySet, Syscall};
    use std::str::FromStr;

    #[test]
    fn capability_parses_from_stable_identifiers() {
        assert_eq!(Capability::from_str("log").unwrap(), Capability::Log);
        assert_eq!(
            Capability::from_str("net.http.listen").unwrap(),
            Capability::NetHttpListen
        );
    }

    #[test]
    fn unknown_capability_fails_clearly() {
        let error = Capability::from_str("net.tcp.connect").expect_err("unknown must fail");
        assert_eq!(error.to_string(), "unknown capability `net.tcp.connect`");
        assert_eq!(error.input(), "net.tcp.connect");
    }

    #[test]
    fn empty_capability_set_denies_all_caps() {
        let set = CapabilitySet::empty();
        assert!(!set.contains(Capability::Log));
        assert!(!set.contains(Capability::NetHttpListen));
    }

    #[test]
    fn capability_set_parses_from_identifiers() {
        let set =
            CapabilitySet::try_from_iter(["log", "net.http.listen"]).expect("known caps parse");
        assert!(set.contains(Capability::Log));
        assert!(set.contains(Capability::NetHttpListen));
    }

    #[test]
    fn missing_log_capability_denies_log_syscall_with_actionable_error() {
        let set = CapabilitySet::empty();
        let error = set
            .enforce(Syscall::LogEmit)
            .expect_err("log must be denied without capability");
        assert_eq!(error.syscall(), Syscall::LogEmit);
        assert_eq!(error.required_capability(), Capability::Log);
        assert_eq!(
            error.to_string(),
            "capability denied: syscall `log.emit` requires capability `log`. Hint: add `allow log`. Declare it in your `.dot` file with an `allow ...` statement."
        );
    }

    #[test]
    fn missing_http_listen_capability_denies_listen_syscall_with_actionable_error() {
        let set = CapabilitySet::empty();
        let error = set
            .enforce(Syscall::NetHttpListen)
            .expect_err("listen must be denied without capability");
        assert_eq!(error.syscall(), Syscall::NetHttpListen);
        assert_eq!(error.required_capability(), Capability::NetHttpListen);
        assert_eq!(
            error.to_string(),
            "capability denied: syscall `net.http.listen` requires capability `net.http.listen`. Hint: add `allow net.http.listen`. Declare it in your `.dot` file with an `allow ...` statement."
        );
    }
}

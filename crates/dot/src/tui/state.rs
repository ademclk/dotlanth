#![forbid(unsafe_code)]

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum Mode {
    Demo,
    Dev,
}

impl Mode {
    pub(crate) const ALL: [Self; 2] = [Self::Demo, Self::Dev];

    pub(crate) fn label(self) -> &'static str {
        match self {
            Self::Demo => "Demo",
            Self::Dev => "Dev",
        }
    }

    pub(crate) fn next(self) -> Self {
        match self {
            Self::Demo => Self::Dev,
            Self::Dev => Self::Demo,
        }
    }

    pub(crate) fn previous(self) -> Self {
        self.next()
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum Capability {
    ParseValidate,
    RunServe,
    ReplayRecovery,
    SecurityCapabilities,
    StateDb,
    ArtifactsInspection,
}

impl Capability {
    pub(crate) const ALL: [Self; 6] = [
        Self::ParseValidate,
        Self::RunServe,
        Self::ReplayRecovery,
        Self::SecurityCapabilities,
        Self::StateDb,
        Self::ArtifactsInspection,
    ];

    pub(crate) fn label(self) -> &'static str {
        match self {
            Self::ParseValidate => "Parse & Validate (dot_dsl, dot_rt)",
            Self::RunServe => "Run & Serve (dot, dot_vm, dot_ops, dot_rt)",
            Self::ReplayRecovery => "Replay & Recovery (dot, dot_artifacts, dot_db)",
            Self::SecurityCapabilities => "Security & Capabilities (dot_sec, dot_vm, dot_ops)",
            Self::StateDb => "State & DB (dot_db)",
            Self::ArtifactsInspection => "Artifacts & Inspection (dot_artifacts, dot_db, dot)",
        }
    }

    pub(crate) fn next(self) -> Self {
        let index = Self::ALL
            .iter()
            .position(|candidate| *candidate == self)
            .unwrap_or(0);
        Self::ALL[(index + 1) % Self::ALL.len()]
    }

    pub(crate) fn previous(self) -> Self {
        let index = Self::ALL
            .iter()
            .position(|candidate| *candidate == self)
            .unwrap_or(0);
        Self::ALL[(index + Self::ALL.len() - 1) % Self::ALL.len()]
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum Focus {
    Modes,
    Capabilities,
    Actions,
    Runs,
    Output,
}

impl Focus {
    pub(crate) fn next(self) -> Self {
        match self {
            Self::Modes => Self::Capabilities,
            Self::Capabilities => Self::Actions,
            Self::Actions => Self::Runs,
            Self::Runs => Self::Output,
            Self::Output => Self::Modes,
        }
    }

    pub(crate) fn previous(self) -> Self {
        match self {
            Self::Modes => Self::Output,
            Self::Capabilities => Self::Modes,
            Self::Actions => Self::Capabilities,
            Self::Runs => Self::Actions,
            Self::Output => Self::Runs,
        }
    }
}

#![forbid(unsafe_code)]

use dot_dsl::Document;
use dot_sec::{CapabilitySet, UnknownCapabilityError};

/// Runtime context derived from a validated dotDSL document.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct RuntimeContext {
    capabilities: CapabilitySet,
}

impl RuntimeContext {
    /// Constructs a new runtime context from an explicit capability set.
    pub fn new(capabilities: CapabilitySet) -> Self {
        Self { capabilities }
    }

    /// Builds a runtime context from a validated dotDSL `Document`.
    ///
    /// This is the only source of capability grants: no defaults are implied.
    pub fn from_dot_dsl(document: &Document) -> Result<Self, UnknownCapabilityError> {
        let capabilities = CapabilitySet::try_from_iter(
            document.capabilities.iter().map(|cap| cap.value.as_str()),
        )?;
        Ok(Self { capabilities })
    }

    /// Returns the granted capabilities for this runtime.
    pub fn capabilities(&self) -> &CapabilitySet {
        &self.capabilities
    }
}

#[cfg(test)]
mod tests {
    use super::RuntimeContext;
    use dot_dsl::parse_and_validate;

    #[test]
    fn runtime_context_derives_capabilities_from_dotdsl_only() {
        let document = parse_and_validate(
            r#"
dot 0.1
app "x"
allow log

api "public"
  route GET "/hello"
    respond 200 "ok"
  end
end
"#,
            "inline.dot",
        )
        .expect("document must validate");

        let ctx = RuntimeContext::from_dot_dsl(&document).expect("capabilities must parse");
        assert!(ctx.capabilities().contains(dot_sec::Capability::Log));
        assert!(
            !ctx.capabilities()
                .contains(dot_sec::Capability::NetHttpListen)
        );
    }
}

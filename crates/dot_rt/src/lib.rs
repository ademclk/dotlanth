#![forbid(unsafe_code)]

use dot_dsl::Document;
use dot_ops::{SourceRef, SourceSpan};
use dot_sec::{Capability, CapabilitySet, UnknownCapabilityError};

/// A capability declaration captured from dotDSL with stable source metadata.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DeclaredCapability {
    capability: Capability,
    source: SourceRef,
}

impl DeclaredCapability {
    pub fn new(capability: Capability, source: SourceRef) -> Self {
        Self { capability, source }
    }

    pub fn capability(&self) -> Capability {
        self.capability
    }

    pub fn source(&self) -> &SourceRef {
        &self.source
    }
}

/// Runtime context derived from a validated dotDSL document.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct RuntimeContext {
    capabilities: CapabilitySet,
    declared_capabilities: Vec<DeclaredCapability>,
}

impl RuntimeContext {
    /// Constructs a new runtime context from an explicit capability set.
    pub fn new(capabilities: CapabilitySet) -> Self {
        Self {
            capabilities,
            declared_capabilities: Vec::new(),
        }
    }

    /// Builds a runtime context from a validated dotDSL `Document`.
    ///
    /// This is the only source of capability grants: no defaults are implied.
    pub fn from_dot_dsl(document: &Document) -> Result<Self, UnknownCapabilityError> {
        let mut capabilities = CapabilitySet::empty();
        let mut declared_capabilities = Vec::with_capacity(document.capabilities.len());
        for (index, capability) in document.capabilities.iter().enumerate() {
            let parsed = capability.value.parse()?;
            capabilities.insert(parsed);
            declared_capabilities.push(DeclaredCapability::new(
                parsed,
                SourceRef::with_span_and_path(
                    SourceSpan::from(capability.span),
                    format!("capabilities[{index}]"),
                ),
            ));
        }
        Ok(Self {
            capabilities,
            declared_capabilities,
        })
    }

    /// Returns the granted capabilities for this runtime.
    pub fn capabilities(&self) -> &CapabilitySet {
        &self.capabilities
    }

    /// Returns capability declarations with their stable source metadata.
    pub fn declared_capabilities(&self) -> &[DeclaredCapability] {
        &self.declared_capabilities
    }

    /// Returns the declaration source for a granted capability, if present.
    pub fn declared_capability_source(&self, capability: Capability) -> Option<&SourceRef> {
        self.declared_capabilities
            .iter()
            .find(|decl| decl.capability() == capability)
            .map(DeclaredCapability::source)
    }
}

#[cfg(test)]
mod tests {
    use super::RuntimeContext;
    use dot_dsl::parse_and_validate;
    use dot_ops::{SourceRef, SourceSpan};
    use dot_sec::Capability;

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

    #[test]
    fn runtime_context_preserves_capability_declaration_sources() {
        let document = parse_and_validate(
            r#"
dot 0.1
app "x"
allow log
allow net.http.listen

server listen 8080

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
        assert_eq!(ctx.declared_capabilities().len(), 2);
        assert_eq!(ctx.declared_capabilities()[0].capability(), Capability::Log);
        assert_eq!(
            ctx.declared_capabilities()[0].source(),
            &SourceRef::with_span_and_path(
                SourceSpan::from(document.capabilities[0].span),
                "capabilities[0]",
            )
        );
        assert_eq!(
            ctx.declared_capability_source(Capability::NetHttpListen),
            Some(&SourceRef::with_span_and_path(
                SourceSpan::from(document.capabilities[1].span),
                "capabilities[1]",
            ))
        );
        assert_ne!(ctx, RuntimeContext::new(ctx.capabilities().clone()));
    }
}

#![forbid(unsafe_code)]

use dot_dsl::Document;
use dot_ops::{SourceRef, SourceSpan};
use dot_sec::{Capability, CapabilitySet, UnknownCapabilityError};

/// The execution contract requested for a runtime.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum DeterminismMode {
    #[default]
    Default,
    Strict,
}

impl DeterminismMode {
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Default => "default",
            Self::Strict => "strict",
        }
    }
}

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
    determinism_mode: DeterminismMode,
}

impl RuntimeContext {
    /// Constructs a new runtime context from an explicit capability set.
    pub fn new(capabilities: CapabilitySet) -> Self {
        Self::new_with_mode(capabilities, DeterminismMode::Default)
    }

    /// Constructs a new runtime context from an explicit capability set and determinism mode.
    pub fn new_with_mode(capabilities: CapabilitySet, determinism_mode: DeterminismMode) -> Self {
        Self {
            capabilities,
            declared_capabilities: Vec::new(),
            determinism_mode,
        }
    }

    /// Builds a runtime context from a validated dotDSL `Document`.
    ///
    /// This is the only source of capability grants: no defaults are implied.
    pub fn from_dot_dsl(document: &Document) -> Result<Self, UnknownCapabilityError> {
        Self::from_dot_dsl_with_mode(document, DeterminismMode::Default)
    }

    /// Builds a runtime context from a validated dotDSL `Document` and explicit mode.
    pub fn from_dot_dsl_with_mode(
        document: &Document,
        determinism_mode: DeterminismMode,
    ) -> Result<Self, UnknownCapabilityError> {
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
            determinism_mode,
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

    /// Returns the selected determinism mode for this runtime.
    pub fn determinism_mode(&self) -> DeterminismMode {
        self.determinism_mode
    }
}

#[cfg(test)]
mod tests {
    use super::{DeclaredCapability, RuntimeContext};
    use dot_dsl::{Document, Span, Spanned, parse_and_validate};
    use dot_ops::{SourceRef, SourceSpan};
    use dot_sec::{Capability, CapabilitySet};

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

    #[test]
    fn runtime_context_new_keeps_declared_metadata_empty() {
        let mut capabilities = CapabilitySet::empty();
        capabilities.insert(Capability::Log);

        let ctx = RuntimeContext::new(capabilities);

        assert!(ctx.capabilities().contains(Capability::Log));
        assert!(ctx.declared_capabilities().is_empty());
        assert_eq!(ctx.declared_capability_source(Capability::Log), None);
    }

    #[test]
    fn runtime_context_handles_documents_without_allows() {
        let document = parse_and_validate(
            r#"
dot 0.1
app "x"

api "public"
  route GET "/hello"
    respond 200 "ok"
  end
end
"#,
            "inline.dot",
        )
        .expect("document must validate");

        let ctx = RuntimeContext::from_dot_dsl(&document).expect("context must build");

        assert_eq!(ctx.capabilities(), &CapabilitySet::empty());
        assert!(ctx.declared_capabilities().is_empty());
        assert_eq!(ctx.declared_capability_source(Capability::Log), None);
        assert_eq!(
            ctx.declared_capability_source(Capability::NetHttpListen),
            None
        );
    }

    #[test]
    fn runtime_context_can_store_selected_determinism_mode() {
        let document = parse_and_validate(
            r#"
dot 0.1
app "x"

api "public"
  route GET "/hello"
    respond 200 "ok"
  end
end
"#,
            "inline.dot",
        )
        .expect("document must validate");

        let ctx = RuntimeContext::from_dot_dsl_with_mode(
            &document,
            super::DeterminismMode::Strict,
        )
        .expect("context must build");

        assert_eq!(ctx.determinism_mode(), super::DeterminismMode::Strict);
    }

    #[test]
    fn runtime_context_propagates_unknown_capability_errors_from_raw_documents() {
        let mut document = Document::default();
        document.capabilities.push(Spanned::new(
            "net.tcp.connect".to_owned(),
            Span::new(1, 1, 15),
        ));

        let error =
            RuntimeContext::from_dot_dsl(&document).expect_err("unknown capability must fail");

        assert_eq!(error.input(), "net.tcp.connect");
        assert_eq!(error.to_string(), "unknown capability `net.tcp.connect`");
    }

    #[test]
    fn declared_capability_round_trips_capability_and_source() {
        let source = SourceRef::with_span_and_path(SourceSpan::new(4, 7, 3), "capabilities[0]");
        let declared = DeclaredCapability::new(Capability::Log, source.clone());

        assert_eq!(declared.capability(), Capability::Log);
        assert_eq!(declared.source(), &source);
    }
}

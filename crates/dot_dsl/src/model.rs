use crate::Span;

/// Generic typed value with source span.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Spanned<T> {
    pub value: T,
    pub span: Span,
}

impl<T> Spanned<T> {
    pub const fn new(value: T, span: Span) -> Self {
        Self { value, span }
    }
}

/// Top-level dotDSL document (v0.1).
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Document {
    pub version: Option<Spanned<String>>,
    pub metadata: Metadata,
    pub capabilities: Vec<Spanned<String>>,
    pub server: Option<Server>,
    pub apis: Vec<Api>,
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Metadata {
    pub app: Option<Spanned<String>>,
    pub project: Option<Spanned<String>>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Server {
    pub port: Spanned<u16>,
    pub span: Span,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Api {
    pub name: Spanned<String>,
    pub routes: Vec<Route>,
    pub span: Span,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Route {
    pub verb: Spanned<String>,
    pub path: Spanned<String>,
    pub response: Option<Response>,
    pub span: Span,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Response {
    pub status: Spanned<u16>,
    pub body: Spanned<String>,
    pub span: Span,
}

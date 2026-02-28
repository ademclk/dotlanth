use std::path::Path;

use crate::diagnostics::{Diagnostic, LoadError, Span};
use crate::model::{Document, Route};

const SUPPORTED_VERSION: &str = "0.1";
const ALLOWED_VERBS: &[&str] = &["GET", "POST", "PUT", "PATCH", "DELETE"];
const KNOWN_CAPABILITIES: &[&str] = &["log", "net.http.listen"];

pub(crate) fn validate(document: &Document, source_path: &Path) -> Result<(), LoadError> {
    let mut diagnostics = Vec::new();

    match &document.version {
        Some(version) if version.value == SUPPORTED_VERSION => {}
        Some(version) => diagnostics.push(Diagnostic::new(
            "version",
            version.span,
            format!(
                "unsupported dot version `{}`; expected `{SUPPORTED_VERSION}`",
                version.value
            ),
        )),
        None => diagnostics.push(Diagnostic::new(
            "version",
            fallback_span(document),
            "missing required `dot` version directive",
        )),
    }

    if document.metadata.app.is_none() && document.metadata.project.is_none() {
        diagnostics.push(Diagnostic::new(
            "metadata",
            fallback_span(document),
            "missing required metadata; expected `app` or `project`",
        ));
    }

    let mut has_listen_capability = false;
    for (index, capability) in document.capabilities.iter().enumerate() {
        if capability.value == "net.http.listen" {
            has_listen_capability = true;
        }
        if !KNOWN_CAPABILITIES.contains(&capability.value.as_str()) {
            diagnostics.push(Diagnostic::new(
                format!("capabilities[{index}]"),
                capability.span,
                format!("unknown capability `{}`", capability.value),
            ));
        }
    }

    if let Some(server) = &document.server {
        if !has_listen_capability {
            diagnostics.push(Diagnostic::new(
                "capabilities",
                server.span,
                "missing required capability `net.http.listen` for `server listen`",
            ));
        }
        if server.port.value == 0 {
            diagnostics.push(Diagnostic::new(
                "server.port",
                server.port.span,
                "server port must be in range 1..=65535",
            ));
        }
    }

    if document.apis.is_empty() {
        diagnostics.push(Diagnostic::new(
            "apis",
            fallback_span(document),
            "at least one `api` block is required",
        ));
    }

    for (api_index, api) in document.apis.iter().enumerate() {
        if api.routes.is_empty() {
            diagnostics.push(Diagnostic::new(
                format!("apis[{api_index}].routes"),
                api.span,
                "api must contain at least one route",
            ));
        }

        for (route_index, route) in api.routes.iter().enumerate() {
            validate_route(route, api_index, route_index, &mut diagnostics);
        }
    }

    if diagnostics.is_empty() {
        Ok(())
    } else {
        Err(LoadError::diagnostics(
            source_path.to_path_buf(),
            diagnostics,
        ))
    }
}

fn validate_route(
    route: &Route,
    api_index: usize,
    route_index: usize,
    diagnostics: &mut Vec<Diagnostic>,
) {
    let base = format!("apis[{api_index}].routes[{route_index}]");

    if !ALLOWED_VERBS.contains(&route.verb.value.as_str()) {
        diagnostics.push(Diagnostic::new(
            format!("{base}.verb"),
            route.verb.span,
            format!("unknown HTTP verb `{}`", route.verb.value),
        ));
    }

    if !route.path.value.starts_with('/') {
        diagnostics.push(Diagnostic::new(
            format!("{base}.path"),
            route.path.span,
            "route path must start with `/`",
        ));
    }
    if route.path.value.chars().any(char::is_whitespace) {
        diagnostics.push(Diagnostic::new(
            format!("{base}.path"),
            route.path.span,
            "route path cannot contain whitespace",
        ));
    }

    match &route.response {
        Some(response) => {
            if !(100..=599).contains(&response.status.value) {
                diagnostics.push(Diagnostic::new(
                    format!("{base}.response.status"),
                    response.status.span,
                    "response status must be in range 100..=599",
                ));
            }
        }
        None => diagnostics.push(Diagnostic::new(
            format!("{base}.response"),
            route.span,
            "missing required `respond` statement",
        )),
    }
}

fn fallback_span(document: &Document) -> Span {
    if let Some(version) = &document.version {
        return version.span;
    }
    if let Some(app) = &document.metadata.app {
        return app.span;
    }
    if let Some(project) = &document.metadata.project {
        return project.span;
    }
    document
        .server
        .as_ref()
        .map(|server| server.span)
        .or_else(|| document.apis.first().map(|api| api.span))
        .unwrap_or(Span::new(1, 1, 1))
}

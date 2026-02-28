use std::path::{Path, PathBuf};

use dot_dsl::{Diagnostic, LoadError, load_and_validate};

#[test]
fn hello_api_fixture_validates() {
    let document = load_and_validate(fixture("hello-api.dot")).expect("fixture should validate");

    assert_eq!(
        document
            .version
            .as_ref()
            .expect("version must exist after validation")
            .value,
        "0.1"
    );
    assert_eq!(
        document
            .metadata
            .app
            .as_ref()
            .expect("app metadata must exist after validation")
            .value,
        "hello-api"
    );
    assert_eq!(document.apis.len(), 1);
    assert_eq!(document.apis[0].routes.len(), 1);
}

#[test]
fn denial_fixture_reports_capability_error_with_stable_path_and_span() {
    let error = load_and_validate(fixture("hello-api-deny.dot"))
        .expect_err("denial fixture must fail capability checks");

    assert_eq!(
        snapshot(&error),
        "7:1:18 capabilities | missing required capability `net.http.listen` for `server listen`"
    );
}

#[test]
fn missing_required_version_is_reported() {
    let error = load_and_validate(fixture("invalid-missing-version.dot"))
        .expect_err("missing version fixture must fail");

    assert_eq!(
        snapshot(&error),
        "1:5:17 version | missing required `dot` version directive"
    );
}

#[test]
fn invalid_route_path_is_reported() {
    let error = load_and_validate(fixture("invalid-route-path.dot"))
        .expect_err("invalid route path fixture must fail");

    assert_eq!(
        snapshot(&error),
        "8:13:7 apis[0].routes[0].path | route path must start with `/`"
    );
}

#[test]
fn unknown_verb_is_reported() {
    let error =
        load_and_validate(fixture("invalid-unknown-verb.dot")).expect_err("unknown verb must fail");

    assert_eq!(
        snapshot(&error),
        "8:9:5 apis[0].routes[0].verb | unknown HTTP verb `FETCH`"
    );
}

#[test]
fn unknown_statement_is_rejected_fail_closed() {
    let error = load_and_validate(fixture("invalid-unknown-statement.dot"))
        .expect_err("unknown statement must fail");

    assert_eq!(
        snapshot(&error),
        "5:1:14 root | unknown statement `unknown`"
    );
}

#[test]
fn missing_response_statement_is_reported() {
    let error = load_and_validate(fixture("invalid-missing-response.dot"))
        .expect_err("route without response must fail");

    assert_eq!(
        snapshot(&error),
        "8:3:18 apis[0].routes[0].response | missing required `respond` statement"
    );
}

#[test]
fn duplicate_dot_statement_is_rejected() {
    let error = load_and_validate(fixture("invalid-duplicate-dot.dot"))
        .expect_err("duplicate dot must fail");

    assert_eq!(
        snapshot(&error),
        "2:1:7 version | duplicate `dot` version directive"
    );
}

#[test]
fn duplicate_app_statement_is_rejected() {
    let error = load_and_validate(fixture("invalid-duplicate-app.dot"))
        .expect_err("duplicate app must fail");

    assert_eq!(
        snapshot(&error),
        "3:1:12 metadata.app | duplicate `app` statement"
    );
}

#[test]
fn duplicate_project_statement_is_rejected() {
    let error = load_and_validate(fixture("invalid-duplicate-project.dot"))
        .expect_err("duplicate project must fail");

    assert_eq!(
        snapshot(&error),
        "3:1:16 metadata.project | duplicate `project` statement"
    );
}

#[test]
fn duplicate_server_statement_is_rejected() {
    let error = load_and_validate(fixture("invalid-duplicate-server.dot"))
        .expect_err("duplicate server must fail");

    assert_eq!(
        snapshot(&error),
        "3:1:18 server | duplicate `server` statement"
    );
}

#[test]
fn display_includes_span_length() {
    let source = "dot 0.1\nunknown \"x\"\n";
    let error =
        dot_dsl::parse_and_validate(source, "<test>").expect_err("unknown statement must fail");

    assert_eq!(
        error.to_string(),
        "<test>:2:1:11 root | unknown statement `unknown`"
    );
}

fn fixture(name: &str) -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("fixtures")
        .join(name)
}

fn snapshot(error: &LoadError) -> String {
    let diagnostics = error
        .as_diagnostics()
        .expect("expected diagnostics-backed error");
    let mut lines = Vec::with_capacity(diagnostics.len());
    for diagnostic in diagnostics {
        lines.push(snapshot_line(diagnostic));
    }
    lines.join("\n")
}

fn snapshot_line(diagnostic: &Diagnostic) -> String {
    format!(
        "{}:{}:{} {} | {}",
        diagnostic.span.line,
        diagnostic.span.column,
        diagnostic.span.length,
        diagnostic.semantic_path,
        diagnostic.message
    )
}

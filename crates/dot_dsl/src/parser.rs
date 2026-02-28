use std::path::Path;

use crate::diagnostics::{Diagnostic, LoadError, Span};
use crate::model::{Api, Document, Response, Route, Server, Spanned};

pub(crate) fn parse(source: &str, source_path: &Path) -> Result<Document, LoadError> {
    let mut document = Document::default();
    let mut context_stack = Vec::new();
    let mut last_line = 1;

    for (line_idx, raw_line) in source.lines().enumerate() {
        let line_no = line_idx + 1;
        last_line = line_no;
        let Some((line, line_column)) = statement_line(raw_line) else {
            continue;
        };

        if line.is_empty() || line.starts_with('#') {
            continue;
        }

        match context_stack.last().copied() {
            None => with_path(
                parse_top_level(
                    line,
                    line_no,
                    line_column,
                    &mut document,
                    &mut context_stack,
                ),
                source_path,
            )?,
            Some(Context::Api { api_index }) => with_path(
                parse_api_line(
                    line,
                    line_no,
                    line_column,
                    &mut document,
                    &mut context_stack,
                    api_index,
                ),
                source_path,
            )?,
            Some(Context::Route {
                api_index,
                route_index,
            }) => with_path(
                parse_route_line(
                    line,
                    line_no,
                    line_column,
                    &mut document,
                    &mut context_stack,
                    api_index,
                    route_index,
                ),
                source_path,
            )?,
        }
    }

    if let Some(ctx) = context_stack.last().copied() {
        let semantic_path = match ctx {
            Context::Api { api_index } => format!("apis[{api_index}]"),
            Context::Route {
                api_index,
                route_index,
            } => format!("apis[{api_index}].routes[{route_index}]"),
        };
        let diagnostic = Diagnostic::new(
            semantic_path,
            Span::new(last_line + 1, 1, 1),
            "unclosed block; expected `end`",
        );
        return Err(LoadError::diagnostics(
            source_path.to_path_buf(),
            vec![diagnostic],
        ));
    }

    Ok(document)
}

#[derive(Clone, Copy)]
enum Context {
    Api {
        api_index: usize,
    },
    Route {
        api_index: usize,
        route_index: usize,
    },
}

fn parse_top_level(
    line: &str,
    line_no: usize,
    line_column: usize,
    document: &mut Document,
    context_stack: &mut Vec<Context>,
) -> Result<(), LoadError> {
    if line == "end" {
        return Err(single_diag(
            "root",
            Span::new(line_no, line_column, 3),
            "unexpected `end` at top level",
        ));
    }

    if line == "dot" || line.starts_with("dot ") {
        if document.version.is_some() {
            return Err(single_diag(
                "version",
                Span::new(line_no, line_column, line.len()),
                "duplicate `dot` version directive",
            ));
        }
        let rest = line.strip_prefix("dot").expect("prefix checked");
        let (version, version_span) = parse_single_token(
            rest,
            line_no,
            line_column + 3,
            "version",
            "missing dot version",
        )?;
        document.version = Some(Spanned::new(version, version_span));
        return Ok(());
    }

    if line == "app" || line.starts_with("app ") {
        if document.metadata.app.is_some() {
            return Err(single_diag(
                "metadata.app",
                Span::new(line_no, line_column, line.len()),
                "duplicate `app` statement",
            ));
        }
        let rest = line.strip_prefix("app").expect("prefix checked");
        let (app_name, app_span) = parse_quoted(
            rest,
            line_no,
            line_column + 3,
            "metadata.app",
            "app name must be quoted",
        )?;
        document.metadata.app = Some(Spanned::new(app_name, app_span));
        return Ok(());
    }

    if line == "project" || line.starts_with("project ") {
        if document.metadata.project.is_some() {
            return Err(single_diag(
                "metadata.project",
                Span::new(line_no, line_column, line.len()),
                "duplicate `project` statement",
            ));
        }
        let rest = line.strip_prefix("project").expect("prefix checked");
        let (project_name, project_span) = parse_quoted(
            rest,
            line_no,
            line_column + 7,
            "metadata.project",
            "project name must be quoted",
        )?;
        document.metadata.project = Some(Spanned::new(project_name, project_span));
        return Ok(());
    }

    if line == "allow" || line.starts_with("allow ") {
        let rest = line.strip_prefix("allow").expect("prefix checked");
        let (capability, capability_span) = parse_single_token(
            rest,
            line_no,
            line_column + 5,
            "capabilities",
            "missing capability name",
        )?;
        document
            .capabilities
            .push(Spanned::new(capability, capability_span));
        return Ok(());
    }

    if line == "server" || line.starts_with("server ") {
        if document.server.is_some() {
            return Err(single_diag(
                "server",
                Span::new(line_no, line_column, line.len()),
                "duplicate `server` statement",
            ));
        }
        let rest = line.strip_prefix("server").expect("prefix checked");
        let (verb, verb_span) = parse_single_token_prefix(
            rest,
            line_no,
            line_column + 6,
            "server",
            "missing server verb",
        )?;
        if verb != "listen" {
            return Err(single_diag(
                "server",
                verb_span,
                format!("unknown server verb `{verb}`"),
            ));
        }
        let (_, remainder_start) = trim_start_with_offset(rest);
        let after_verb = &rest[remainder_start + verb.len()..];
        let (port_raw, port_span) = parse_single_token(
            after_verb,
            line_no,
            verb_span.column + verb_span.length,
            "server.port",
            "missing server listen port",
        )?;
        let port = parse_u16(
            port_raw.as_str(),
            port_span,
            "server.port",
            "invalid server port",
        )?;
        document.server = Some(Server {
            port: Spanned::new(port, port_span),
            span: Span::new(line_no, line_column, line.len()),
        });
        return Ok(());
    }

    if line == "api" || line.starts_with("api ") {
        let rest = line.strip_prefix("api").expect("prefix checked");
        let (name, name_span) = parse_quoted(
            rest,
            line_no,
            line_column + 3,
            "apis",
            "api name must be quoted",
        )?;
        let api_index = document.apis.len();
        document.apis.push(Api {
            name: Spanned::new(name, name_span),
            routes: Vec::new(),
            span: Span::new(line_no, line_column, line.len()),
        });
        context_stack.push(Context::Api { api_index });
        return Ok(());
    }

    Err(single_diag(
        "root",
        Span::new(line_no, line_column, line.len()),
        format!("unknown statement `{}`", first_token(line)),
    ))
}

fn parse_api_line(
    line: &str,
    line_no: usize,
    line_column: usize,
    document: &mut Document,
    context_stack: &mut Vec<Context>,
    api_index: usize,
) -> Result<(), LoadError> {
    if line == "end" {
        context_stack.pop();
        return Ok(());
    }

    if line == "route" || line.starts_with("route ") {
        let rest = line.strip_prefix("route").expect("prefix checked");
        let (verb, verb_span) = parse_single_token_prefix(
            rest,
            line_no,
            line_column + 5,
            format!("apis[{api_index}].routes"),
            "missing route verb",
        )?;
        let (_, remainder_start) = trim_start_with_offset(rest);
        let after_verb = &rest[remainder_start + verb.len()..];
        let (path_value, path_span) = parse_quoted(
            after_verb,
            line_no,
            verb_span.column + verb_span.length,
            format!("apis[{api_index}].routes"),
            "route path must be quoted",
        )?;
        let route_index = document.apis[api_index].routes.len();
        document.apis[api_index].routes.push(Route {
            verb: Spanned::new(verb, verb_span),
            path: Spanned::new(path_value, path_span),
            response: None,
            span: Span::new(line_no, line_column, line.len()),
        });
        context_stack.push(Context::Route {
            api_index,
            route_index,
        });
        return Ok(());
    }

    Err(single_diag(
        format!("apis[{api_index}]"),
        Span::new(line_no, line_column, line.len()),
        format!("unknown statement `{}`", first_token(line)),
    ))
}

fn parse_route_line(
    line: &str,
    line_no: usize,
    line_column: usize,
    document: &mut Document,
    context_stack: &mut Vec<Context>,
    api_index: usize,
    route_index: usize,
) -> Result<(), LoadError> {
    if line == "end" {
        context_stack.pop();
        return Ok(());
    }

    if line == "respond" || line.starts_with("respond ") {
        let rest = line.strip_prefix("respond").expect("prefix checked");
        let semantic_base = format!("apis[{api_index}].routes[{route_index}]");
        let (status_raw, status_span) = parse_single_token_prefix(
            rest,
            line_no,
            line_column + 7,
            format!("{semantic_base}.response"),
            "missing response status code",
        )?;
        let status = parse_u16(
            status_raw.as_str(),
            status_span,
            format!("{semantic_base}.response.status"),
            "response status must be an integer",
        )?;
        let (_, remainder_start) = trim_start_with_offset(rest);
        let after_status = &rest[remainder_start + status_raw.len()..];
        let (body, body_span) = parse_quoted(
            after_status,
            line_no,
            status_span.column + status_span.length,
            format!("{semantic_base}.response.body"),
            "response body must be quoted",
        )?;
        let route = &mut document.apis[api_index].routes[route_index];
        if route.response.is_some() {
            return Err(single_diag(
                format!("{semantic_base}.response"),
                Span::new(line_no, line_column, line.len()),
                "duplicate `respond` statement",
            ));
        }
        route.response = Some(Response {
            status: Spanned::new(status, status_span),
            body: Spanned::new(body, body_span),
            span: Span::new(line_no, line_column, line.len()),
        });
        return Ok(());
    }

    Err(single_diag(
        format!("apis[{api_index}].routes[{route_index}]"),
        Span::new(line_no, line_column, line.len()),
        format!("unknown statement `{}`", first_token(line)),
    ))
}

fn parse_single_token(
    input: &str,
    line_no: usize,
    base_column: usize,
    semantic_path: impl Into<String>,
    missing_message: &str,
) -> Result<(String, Span), LoadError> {
    let (trimmed, leading_ws) = trim_start_with_offset(input);
    if trimmed.is_empty() {
        return Err(single_diag(
            semantic_path,
            Span::new(line_no, base_column + leading_ws, 1),
            missing_message,
        ));
    }
    let token_len = trimmed
        .char_indices()
        .find(|(_, ch)| ch.is_whitespace())
        .map(|(idx, _)| idx)
        .unwrap_or(trimmed.len());
    let token = &trimmed[..token_len];
    let trailing = trimmed[token_len..].trim();
    if !trailing.is_empty() {
        let non_whitespace_start = trimmed[token_len..]
            .find(|ch: char| !ch.is_whitespace())
            .unwrap_or(0);
        return Err(single_diag(
            semantic_path,
            Span::new(
                line_no,
                base_column + leading_ws + token_len + non_whitespace_start,
                trailing.len(),
            ),
            "unexpected trailing content",
        ));
    }
    let span = Span::new(line_no, base_column + leading_ws, token.len());
    Ok((token.to_owned(), span))
}

fn parse_single_token_prefix(
    input: &str,
    line_no: usize,
    base_column: usize,
    semantic_path: impl Into<String>,
    missing_message: &str,
) -> Result<(String, Span), LoadError> {
    let (trimmed, leading_ws) = trim_start_with_offset(input);
    if trimmed.is_empty() {
        return Err(single_diag(
            semantic_path,
            Span::new(line_no, base_column + leading_ws, 1),
            missing_message,
        ));
    }
    let token_len = trimmed
        .char_indices()
        .find(|(_, ch)| ch.is_whitespace())
        .map(|(idx, _)| idx)
        .unwrap_or(trimmed.len());
    let token = &trimmed[..token_len];
    let span = Span::new(line_no, base_column + leading_ws, token.len());
    Ok((token.to_owned(), span))
}

fn parse_quoted(
    input: &str,
    line_no: usize,
    base_column: usize,
    semantic_path: impl Into<String>,
    missing_message: &str,
) -> Result<(String, Span), LoadError> {
    let (trimmed, leading_ws) = trim_start_with_offset(input);
    if !trimmed.starts_with('"') {
        return Err(single_diag(
            semantic_path,
            Span::new(line_no, base_column + leading_ws, 1),
            missing_message,
        ));
    }
    let mut escaped = false;
    let mut close_index = None;
    for (idx, ch) in trimmed.char_indices().skip(1) {
        if escaped {
            escaped = false;
            continue;
        }
        if ch == '\\' {
            escaped = true;
            continue;
        }
        if ch == '"' {
            close_index = Some(idx);
            break;
        }
    }
    let Some(close_index) = close_index else {
        return Err(single_diag(
            semantic_path,
            Span::new(line_no, base_column + leading_ws, trimmed.len()),
            "unterminated quoted string",
        ));
    };

    let raw_literal = &trimmed[1..close_index];
    let unescaped = unescape(raw_literal);
    let remaining = trimmed[close_index + 1..].trim();
    if !remaining.is_empty() {
        let trailing_start = trimmed[close_index + 1..]
            .find(|ch: char| !ch.is_whitespace())
            .unwrap_or(0);
        return Err(single_diag(
            semantic_path,
            Span::new(
                line_no,
                base_column + leading_ws + close_index + 1 + trailing_start,
                remaining.len(),
            ),
            "unexpected trailing content",
        ));
    }

    let span = Span::new(line_no, base_column + leading_ws, close_index + 1);
    Ok((unescaped, span))
}

fn parse_u16(
    raw: &str,
    span: Span,
    semantic_path: impl Into<String>,
    invalid_message: &str,
) -> Result<u16, LoadError> {
    raw.parse::<u16>()
        .map_err(|_| single_diag(semantic_path, span, invalid_message))
}

fn single_diag(
    semantic_path: impl Into<String>,
    span: Span,
    message: impl Into<String>,
) -> LoadError {
    LoadError::diagnostics(
        "<inline>",
        vec![Diagnostic::new(semantic_path, span, message)],
    )
}

fn with_path(result: Result<(), LoadError>, source_path: &Path) -> Result<(), LoadError> {
    result.map_err(|error| match error {
        LoadError::Io { .. } => error,
        LoadError::Diagnostics { diagnostics, .. } => {
            LoadError::diagnostics(source_path.to_path_buf(), diagnostics)
        }
    })
}

fn statement_line(raw_line: &str) -> Option<(&str, usize)> {
    let trimmed_start = raw_line.trim_start();
    if trimmed_start.is_empty() {
        return None;
    }
    let leading_ws = raw_line.len() - trimmed_start.len();
    Some((trimmed_start.trim_end(), leading_ws + 1))
}

fn trim_start_with_offset(input: &str) -> (&str, usize) {
    let trimmed = input.trim_start();
    (trimmed, input.len() - trimmed.len())
}

fn first_token(line: &str) -> &str {
    line.split_whitespace().next().unwrap_or(line)
}

fn unescape(value: &str) -> String {
    let mut out = String::with_capacity(value.len());
    let mut chars = value.chars();
    while let Some(ch) = chars.next() {
        if ch == '\\' {
            if let Some(next) = chars.next() {
                match next {
                    '"' => out.push('"'),
                    '\\' => out.push('\\'),
                    'n' => out.push('\n'),
                    't' => out.push('\t'),
                    other => {
                        out.push('\\');
                        out.push(other);
                    }
                }
            } else {
                out.push('\\');
            }
        } else {
            out.push(ch);
        }
    }
    out
}

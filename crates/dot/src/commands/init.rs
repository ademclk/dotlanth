#![forbid(unsafe_code)]

use std::fs::OpenOptions;
use std::io::Write;
use std::path::Path;

const DOT_FILE_NAME: &str = "app.dot";

pub(crate) fn run(dir: &Path) -> Result<(), String> {
    let message = initialize(dir)?;
    println!("{message}");
    Ok(())
}

pub(crate) fn initialize(dir: &Path) -> Result<String, String> {
    ensure_empty_dir(dir)?;

    let app_name = dir
        .file_name()
        .and_then(|name| name.to_str())
        .filter(|name| !name.trim().is_empty())
        .unwrap_or("app");

    write_new_file(
        &dir.join(DOT_FILE_NAME),
        &hello_api_dot(app_name),
        "dot file",
    )?;
    write_new_file(&dir.join(".gitignore"), ".dotlanth/\n", "gitignore file")?;

    Ok(format!("initialized {}", dir.display()))
}

fn ensure_empty_dir(path: &Path) -> Result<(), String> {
    if path.exists() {
        if !path.is_dir() {
            return Err(format!(
                "path exists and is not a directory: `{}`",
                path.display()
            ));
        }
        let mut entries = std::fs::read_dir(path)
            .map_err(|error| format!("failed to read directory `{}`: {error}", path.display()))?;
        if entries.next().is_some() {
            return Err(format!(
                "refusing to initialize into a non-empty directory: `{}`",
                path.display()
            ));
        }
        return Ok(());
    }

    std::fs::create_dir_all(path)
        .map_err(|error| format!("failed to create directory `{}`: {error}", path.display()))?;
    Ok(())
}

fn write_new_file(path: &Path, contents: &str, label: &str) -> Result<(), String> {
    let mut file = OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(path)
        .map_err(|error| format!("failed to create {label} `{}`: {error}", path.display()))?;

    file.write_all(contents.as_bytes())
        .map_err(|error| format!("failed to write {label} `{}`: {error}", path.display()))?;
    Ok(())
}

fn hello_api_dot(app_name: &str) -> String {
    format!(
        r#"dot 0.1

app "{app_name}"

allow log
allow net.http.listen

server listen 8080

api "public"
  route GET "/hello"
    respond 200 "Hello from Dotlanth"
  end
end
"#
    )
}

#[cfg(test)]
mod tests {
    use super::{DOT_FILE_NAME, run};
    use tempfile::TempDir;

    #[test]
    fn init_scaffolds_hello_api_project() {
        let temp = TempDir::new().expect("temp dir must create");
        let root = temp.path().join("hello-api");

        run(&root).expect("init should succeed");

        assert!(root.join(DOT_FILE_NAME).exists());
        assert!(root.join(".gitignore").exists());
    }

    #[test]
    fn init_refuses_non_empty_dir() {
        let temp = TempDir::new().expect("temp dir must create");
        let root = temp.path().join("hello-api");
        std::fs::create_dir_all(&root).expect("dir should create");
        std::fs::write(root.join("existing.txt"), "x").expect("file should write");

        let err = run(&root).expect_err("init must fail");
        assert!(err.contains("non-empty directory"));
    }
}

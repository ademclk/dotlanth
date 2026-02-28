#![forbid(unsafe_code)]

const BANNER: &str = "dot CLI placeholder";

fn main() {
    println!("{BANNER}");
}

#[cfg(test)]
mod tests {
    use super::BANNER;

    #[test]
    fn smoke_banner_is_not_empty() {
        assert!(!BANNER.trim().is_empty());
    }
}

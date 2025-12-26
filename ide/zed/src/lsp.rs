use zed_extension_api::{self as zed, LanguageServerId, Result};

struct ZedVolo {}

impl zed::Extension for ZedVolo {
    fn new() -> Self
    where
        Self: Sized,
    {
        Self {}
    }

    fn language_server_command(
        &mut self,
        _: &LanguageServerId,
        worktree: &zed::Worktree,
    ) -> Result<zed::Command> {
        let mut lsp_args = Vec::new();
        lsp_args.push("--stdio".to_string());

        lsp_args.push("--binders".to_string());
        lsp_args.push(format!(
            "{}/assets/schemas/script_*_binder.json",
            worktree.root_path()
        ));

        Ok(zed::Command {
            command: format!("{}/bin/lsp", worktree.root_path()),
            args: lsp_args,
            env: Default::default(),
        })
    }
}

zed::register_extension!(ZedVolo);

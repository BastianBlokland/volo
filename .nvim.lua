
-- Register script language.
vim.filetype.add({ extension = { script = "voloscript", } })

-- NOTE: For syntax highlighting install the tree-sitter grammar:
-- https://github.com/BastianBlokland/tree-sitter-voloscript

-- Register script LSP.
local root = vim.fn.expand('%:p:h')
local lsp_path = root .. "/bin/lsp"
local binder_path = root .. "/assets/schemas/script_*_binder.json"
if vim.fn.executable(lsp_path) == 1 then
  vim.lsp.config.volo_lsp = {
    cmd = { lsp_path, "--stdio", "--binders", binder_path },
    filetypes = { "voloscript" },
    root_markers = { ".git" },
  }
  vim.lsp.enable("volo_lsp")
else
  vim.notify("volo-lsp: No lsp binary found", vim.log.levels.ERROR)
end

-- Auto format on save.
vim.cmd [[autocmd BufWritePre * lua vim.lsp.buf.format()]]

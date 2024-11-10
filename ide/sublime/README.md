# Sublime language support

Rudimentary configuration files to add volo-script language support to Sublime.

Consists of two parts:
- Sublime-syntax yaml file for basic syntax highlighting.
- LSP (Language Server Protocol) support for code-completion.

## Syntax-file

Documentation: https://www.sublimetext.com/docs/syntax.html

Installation:
* Copy the `volo-script.sublime-syntax` to `~/.config/sublime-text/Packages/User`.
* Select `Volo Script` from the `View` -> `Syntax` dropdown.

## LSP

Documentation: https://lsp.sublimetext.io/

Installation:
* Install the `LSP` package through the `Package Manager`.
* Copy the following text to your `LSP.sublime-settings` file (`~/.config/sublime-text/Packages/User/LSP.sublime-settings`).
```json
{
	"lsp_format_on_save": true,
 	"semantic_highlighting": true,
 	"clients": {
 		"volo-script": {
			"enabled": true,
			"command": [
				"[REPOSITORY_PATH]/build/apps/utilities/app_lsp",
				"--stdio",
				"--binders",
				"[REPOSITORY_PATH]/assets/schemas/script_import_mesh_binder.json"
				"[REPOSITORY_PATH]/assets/schemas/script_scene_binder.json"
			],
			"languageId": "volo-script",
			"scopes": [ "source.volo-script" ]
		}
	}
}
```
NOTE: `[REPOSITORY_PATH]` has to be replaced with the directory path of the volo repository.


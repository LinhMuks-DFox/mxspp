# MXScript (mxs) — VS Code syntax highlighting

A minimal TextMate grammar for `.mxs` files. Highlighting only (no lint / LSP yet).
The keyword, literal, comment and annotation rules are derived directly from the
parser grammar (`include/mxspp/frontend/grammar.hpp`), so they stay in sync with the language.

## What it colors

- Comments: `#` and `//` line comments, `/* … */` blocks
- Strings `"…"` with `\` escapes; `int` / `float` (`1.5`) numbers
- Constants: `true` `false` `nil`
- Keywords: `if else loop until do for in break continue return match case defer assert raise`,
  `import export as`, `func let class interface enum type operator`, `mut static override public private dynamic`
- Annotations: `@@foreign` `@@template` `@@POD` (`@@name`)
- Types: built-ins (`int float bool string decimal Object List Array Tuple Dict Error …`) and
  Capitalized identifiers (class names); `self`
- Function definitions / calls, operators (`-> => .. ** == != <= >= || && …`)

## Install (local, live-editable)

Symlink this folder into your VS Code extensions dir, then reload the window:

```sh
ln -s "$(pwd)/tools/vscode-mxs" ~/.vscode/extensions/mxs-language-0.0.1
```

Then in VS Code: **Cmd+Shift+P → "Developer: Reload Window"**. Open any `.mxs` file —
the language indicator (bottom-right) should read **MXScript**.

To uninstall: `rm ~/.vscode/extensions/mxs-language-0.0.1` and reload.

## Package as a .vsix (optional, for sharing)

```sh
npm i -g @vscode/vsce
cd tools/vscode-mxs && vsce package
# produces mxs-language-0.0.1.vsix → install via "Extensions: Install from VSIX…"
```

## Notes

- This is highlighting only. Diagnostics ("lint" — red squiggles on syntax errors) would wire a
  `mxs check <file>` parse-only mode (not yet a driver subcommand) into VS Code diagnostics; the
  parser already produces `name:line:col: syntax error: …` (see `src/frontend/parser.cpp`), so the
  ground is there for a later pass.

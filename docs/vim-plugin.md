# MarkQL Vim Plugin

## Scope

The classic Vim plugin lives in `editors/vim/` and intentionally stays small:

- filetype detection for dedicated MarkQL extensions
- syntax highlighting for documented MarkQL tokens
- a conservative ftplugin

It does not add:

- semantic linting
- Neovim-only APIs
- custom indentation logic

## Installed Files

- `editors/vim/ftdetect/markql.vim`
- `editors/vim/ftplugin/markql.vim`
- `editors/vim/syntax/markql.vim`

## Installation

Copy the files into matching paths under `~/.vim/`:

```bash
mkdir -p ~/.vim/ftdetect ~/.vim/ftplugin ~/.vim/syntax
cp editors/vim/ftdetect/markql.vim ~/.vim/ftdetect/
cp editors/vim/ftplugin/markql.vim ~/.vim/ftplugin/
cp editors/vim/syntax/markql.vim ~/.vim/syntax/
```

## File Associations

The plugin auto-detects:

- `*.markql`
- `*.mql`
- `*.msql`
- `*.markql.sql`

Existing MarkQL `.sql` files are not claimed automatically. For those, either set the filetype manually:

```vim
:setfiletype markql
```

or add a path-scoped autocmd in your own Vim config.

## Local Buffer Defaults

The ftplugin only sets local options:

- `commentstring=-- %s`
- `comments=:--`
- non-intrusive `formatoptions`
- `iskeyword+=-`

No global options are changed.

# MarkQL Vim Plugin

This package provides a lightweight classic Vim integration for MarkQL:

- filetype detection for `.mql`, `.msql`, `.markql`, and `.markql.sql`
- syntax highlighting for documented MarkQL tokens
- conservative local buffer settings

Install by copying:

- `editors/vim/ftdetect/markql.vim`
- `editors/vim/ftplugin/markql.vim`
- `editors/vim/syntax/markql.vim`

into the matching directories under `~/.vim/`.

The plugin does not auto-claim every `.sql` file. For existing MarkQL `.sql` files, set the filetype manually with:

```vim
:setfiletype markql
```

or add your own local autocmd for selected paths.

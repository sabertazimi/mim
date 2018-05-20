# mim

Minimal vim-like text editor

## Change terminal mode

change mode of terminal from `canonical/cooked mode` (interact when 'enter' pressed)
to `raw mode` (interact immediately)

## Escape Sequences

*   `\x1b[` or `\033[` (`27[`) is the start of an escape sequence for terminal control command
*   Most termimal emulator support [VT100 terminal escape sequences](https://vt100.net/docs/vt100-ug/chapter3.html)

```c
\x1b[0J clear screen before cursor
\x1b[1J clear whole screen
\x1b[2J clear screen after cursor
\x1b[x;yH  move cursor to line x column y
```


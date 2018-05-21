# mim

Minimal vim-like text editor

## Current Features

### Command Mode

*   `h/j/k/l`: move left/down/up/right
*   `0/$`: move to start/end of line
*   `^u / ^d`: page up/down
*   `G`: move to end of file
*   `i`: goto insert mode
*   `\b`: move left
*   `\r`: move down

### Insert Mode

*   `arrow` keys: move left/down/up/right
*   `home/end` keys: move to start/end of line
*   `page up/down` keys: page up/down
*   `esc` key: goto command mode
*   `^s`: save to file

### Lastline Mode

*   `save as` file

### Status Bar

*   filename
*   total lines number
*   current line numebr

## Future Features

*   search bar
*   syntax highlight
*   show line number
*   more command/insert/lastline operations

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

## Ncurses library

http://www.ibiblio.org/pub/Linux/docs/HOWTO/other-formats/pdf/NCURSES-Programming-HOWTO.pdf


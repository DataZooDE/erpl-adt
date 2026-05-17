# Demo: DDIC table definition
# Run from this directory:  vhs ddic.tape

Set Shell "bash"
Set FontFamily "monospace"
Set FontSize 17
Set LetterSpacing 0
Set LineHeight 1.2
Set TypingSpeed 50ms
Set Padding 28
Set Margin 0
Set MarginFill "#000000"
Set WindowBar Colorful
Set BorderRadius 12
Set LoopOffset 25%
Set Theme { "name": "erpl", "black": "#000000", "red": "#ff5f56", "green": "#7ee787", "yellow": "#fff100", "blue": "#79c0ff", "magenta": "#d2a8ff", "cyan": "#56d4dd", "white": "#e6edf3", "brightBlack": "#6e7681", "brightRed": "#ff7b72", "brightGreen": "#7ee787", "brightYellow": "#fff100", "brightBlue": "#79c0ff", "brightMagenta": "#d2a8ff", "brightCyan": "#56d4dd", "brightWhite": "#ffffff", "background": "#000000", "foreground": "#e6edf3", "selection": "#264f78", "cursor": "#e6edf3" }

Output "../ddic.gif"

Set Width 1280
Set Height 660

Hide
Type "export PATH=../staging/bin:$PATH"
Enter
Type "export DEMO_DATA=../staging/data"
Enter
Type `clear && export PS1='\033[1;33m❯\033[0m '`
Enter
Sleep 200ms
Show

Sleep 500ms
Type "erpl-adt ddic table SFLIGHT"
Sleep 400ms
Enter
Sleep 4s

Sleep 1500ms

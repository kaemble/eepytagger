# eepytagger
Simple CLI tool for timestamping livestreams written in C, inspired by the korotagger Discord bot.

## Compilation
Requires readline to be installed.
`gcc eepytagger.c -o eepytagger -lreadline`

## Usage
```
Commands:
  !start [HH:MM:SS]                Start a tagging session, optionally setting an initial timestamp offset.
  !end                             End the tagging session and save to the output file.
  !offset <n>/all +/-<seconds>     Adjust the timestamp of tag(s) <n>/all by +/- seconds.
  !previous +/-<seconds>           Adjust the timestamp of the last tag by +/- seconds.
  !prev +/-<seconds>               Same as !previous.
  !edit <n> <new text>             Change the text of tag <n>, if <n> is not provided it edits the last tag
                                   instead. '$' represents the previous version of the tag (can be escaped).
  !delete <n>                      Delete tag <n>.
  !help                            Show this help message.
  <any text>                       Add a new tag with the current timestamp and the input text.

Command-line arguments:
  -f <output_file>                 Specify output file (default: timestamps.txt).
  -t <temp_file>                   Specify temporary file (default: /tmp/timestamps.txt).
  --resume <file>                  Resume tagging from an existing file.

Use up/down arrow keys to cycle through command history.
Maximum 1000 tags allowed.
```
You can check your current tags by reading the temporary file.

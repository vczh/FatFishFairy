# TODO

- Add progress indicator at the left bottom corner of the main window:
  - "V": the vision agent is on working.
  - "F": the fairy agent is working.
  - A failure counter is set to 0, when a loop fails increase the counter, so it means consecutive failures since the last succeeded one.
  - The failure counter should be appended to the letter, it is omitted when it is 0.
  - The easiest way to do is to add a `<SolidLabel/>`, inheriting the main window's font, but apply bold style and set the color to skyblue.

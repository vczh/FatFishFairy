# TODO

- [x] Add progress indicator at the left bottom corner of the main window:
  - "V": the vision agent is working.
  - "F": the fairy agent is working.
  - There is no idle state so in any moment we should display either "V" or "F".
  - A failure counter is set to 0, when a loop fails increase the counter, so it means consecutive failures since the last succeeded one.
    - Failure includes the signal of exceeding token limit, so when such error happens, we might need to switch from F to F1, F2, ... without V, unless the current round is canceled.
    - A succeeded round immediately reset the failure counter to 0.
  - The failure counter should be appended to the letter, it is omitted when it is 0.
  - The easiest way to do is to add a `<SolidLabel/>`, inheriting the main window's font, but apply bold style and set the color to skyblue.

# broadcast.csv: the shortwave broadcast schedule

`broadcast.csv` is the station schedule behind the Channel List's
**Broadcast Info** (microphone GENE) and **Save as channel** (NW).

## Source

The **EiBi shortwave schedule** by Eike Bierwirth, https://www.eibispace.de/
(the CSV database, `sked-<season>.csv`, unchanged).

This copy is season **A26** (summer 2026, 29 March to 24 October 2026),
from an update of that season taken for 1KO125 1.0.2 (Hany El Imam). EiBi
publishes a new file for each season: **B26** (winter) starts on
25 October 2026, then A27 at the end of March 2027, and updates each file
during its season.

## Terms of use

From EiBi's README.TXT, *Conditions of use*:

> All my frequency lists are free of cost, and any person is absolutely
> free to download, use, copy, or distribute these files or to use them
> within third-party software.

Corrections go to EiBi (the address is in that README).

## Updating it

- In the firmware: replace this file with the season's `sked-<season>.csv`
  from https://www.eibispace.de/dx/ (same format, header line included)
  and update the season above.
- On a radio: the first time Broadcast Info is used, this file is copied to
  `/mnt/broadcast.csv` on the SD card, and from then on only that copy is
  read. A newer firmware does not replace it: to update, copy a newer
  `sked-<season>.csv` there as `broadcast.csv` (or delete it, and the
  firmware's copy is taken again).

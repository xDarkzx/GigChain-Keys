# Chord charts

Every song can show its **chords and lyrics**, with each chord sitting over
the word it changes on. You edit the chart right where you read it, in the
**Chart** tab of the Edit view. On stage the same chart fills the screen
([Perform mode](perform.md)), and [Practice mode](practice.md) turns its
chords into notes to learn.

## Get the chords in

Any of these works:

- **Paste:** copy a song's chords and lyrics from a chord website (Ultimate
  Guitar and the like) or a document, then click **Paste chords** (or press
  **Ctrl+V** in the Chart tab). The site's extras (ads, tabs, menus) are
  cleaned out and each chord is put on the word it sat over. Chord sites
  often place a chord a letter or two into a word; it goes on the start of
  that word. Not happy? **Ctrl+Z** takes the whole paste back in one go.
- **Import file…** reads a chord sheet: *.txt*, *.cho*, *.chopro*,
  *.chordpro*, *.crd*, *.pro*, *.onsong*.
- **Type:** click **+ Click to type a new line** at the bottom and type the
  lyrics. Then add the chords as below.

## Each line is a row of cells

Each word of a line is a cell, with a **chord box** above it. **Click a
line** to open it: a thin bar shows over every word that has no chord yet,
so you can see where chords can go.

```
   G      ▁▁▁▁▁   ▁▁▁    ▁▁▁▁     D     ▁▁▁
   I      found    a     love    for    me
```

### Chords

- **Add one:** click the bar over a word, type the chord (*G*, *Am7*, *D/F#*,
  *C#m7*) and press **Enter**. **Tab** saves it and moves to the next word's
  box, so you can type a whole line of chords in a row.
- **Change one:** click it, type the new name, press **Enter**. Leave it
  empty to take it off.
- **Move one:** drag it onto another word's box, on the same line or another.
  It lands exactly on that word.
- **Right-click** a chord for **How to play it** (its
  [chord diagram](#how-to-play-a-chord)) and **Remove**. You can also drag it
  onto **Remove** at the top.
- **The chord palette** at the top holds the song's chords: drag one onto any
  word. Type a new chord in **Chord…** to drag that in.

### Words

- **Change a word:** double-click it, type, press **Enter** (Tab goes on to
  the next word). The chords stay on their words.
- **Type a whole line:** right-click the line and choose **Edit the line as
  text**. **Enter** saves it and starts a new line after the caret; **Up**
  and **Down** move between lines; **Backspace** at the start joins it to the
  line above; **Esc** leaves it as it was.
- **Add a line below:** right-click the line, **Add a line below**.

Every change is one step for **Ctrl+Z** (undo) and **Ctrl+Shift+Z** (redo).

## Sections and the song's flow

Songs are split into sections: *Intro, Verse, Pre-Chorus, Chorus, Bridge,
Solo, Outro*.

- **+ Section** adds one at the end (pick a name from the list).
- **Double-click a section's title** to rename it (*Verse 2*, *Big chorus*).
- **Click a title** to go to that section.

Under each title you see which instruments play in it and how many bars it
lasts.

The **Flow** bar above the chart says the order the song is really played in,
for example *Verse 1 → Pre-Chorus → Chorus → Verse 2 → Chorus ×3 → Outro*.
It starts as the chart's order. A chord sheet often writes the chorus once,
though the band plays it three times. Click a part for **Play it once more**,
**Move earlier** or **later** and **Take it out**. **+** adds a section of the
chart again, and **Chart order** goes back. Chord follow keeps to this flow:
see [Sections, tempo and backing tracks](sections-and-tempo.md).

Renaming a section (double-click its title) keeps it in the flow. A part
whose section is no longer in the chart (changed in the ChordPro text) shows
dimmed with a **?**: it is skipped, and goes at your next change to the flow.

## How to play a chord

Forgot a chord? **Right-click it → How to play it** here, **tap it** in
Perform, or **tap its name** in Practice. A keyboard shows a dot on each key
to press, ringed **blue** for the left hand and **gold** for the right, with
the notes by name (*Left hand: D#, Right hand: E G# B*).

The buttons under it show the chord's **inversions** (root position, 1st, 2nd,
and 3rd for a four-note chord). **Use this one for … in this song** keeps the
one you like (marked ✓): the diagram opens on it next time, and Practice plays
it when its right hand is set to **My inversions**.

## Edit as text (ChordPro)

**⋯ > Edit as text (ChordPro)** shows the chart as plain text, for those who
like to type it all. Chords go in [brackets] right before the word they
change on, and a line like *{comment: Chorus}* starts a section:

```
{comment: Verse}
[G]Amazing [G7]grace, how [C]sweet the [G]sound
```

Click **Done** to go back.

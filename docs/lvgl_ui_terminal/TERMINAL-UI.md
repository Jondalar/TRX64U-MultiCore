# Terminal UI grammar

The terminal variant is not a retro skin over the card UI. It is a second
presentation grammar for the same information architecture.

## Frame

```text
DIRECT       TRX64/OS  M2 TERMINAL                    [C64:RUN] [D8:ON] IDLE
[F1]HOME     ----------------------------------------------------------------
[F3]LIBRARY  [1]OVERVIEW [2]MONITOR [3]DIAG [4]APPS [5]STREAMS
[F5]C64      ----------------------------------------------------------------
[F7]ACTIONS
              Screen content uses short headings, one-line rows and space.
SHIFTED
[F2]TIMEWARP
[F4]TOOLS
[F6]SETTINGS ----------------------------------------------------------------
[F8]SYSTEM   [CRSR] MOVE [RET] SELECT [<-] BACK/EXIT          [1-5] TAB

[<-]BACK/EXIT
[R/S]CLOSE
[FRZ]TOGGLE
```

At 12 px IBM Plex Mono, the content viewport remains approximately 80 usable
columns. A narrow rail sits beside it for the physical function-key pattern;
the rest of the 960 px canvas remains black.

The rail is the global axis and the numbered line is the local axis. The
mapping is shared with the graphical prototype and specified in
[`../lvgl_ui_project/NAVIGATION.md`](../lvgl_ui_project/NAVIGATION.md).

## Elements

| Purpose | Terminal form |
| --- | --- |
| action | `> Mount selected image                         [PRIMARY]` |
| button | `[ SAVE TO FLASH ]` |
| status | `[ONLINE]`, `[DIRTY]`, `[PAUSED]` |
| switch | `[ON ]` or `[OFF]` |
| slider | `[==========|---------]` |
| progress | `[########################----------------]` |
| hierarchy | whitespace, short headings and `-` dividers |
| focus | inverse green field, never an outline or glow |

ASCII is intentional. Unicode box drawing, icon fonts, bitmap assets and
graphic chrome are outside this variant's visual language.

## Density rules

- Prefer one 32 px action row over a card.
- Keep descriptions on the same line when they remain readable.
- Use 10 px text for metadata and 12 px for normal content.
- Reserve 14 px for the current screen title or a dominant value.
- Keep the selected item, current state and resulting action visible together.
- Long runtime catalogues scroll; they do not become multi-column dashboards.
- Confirmation screens may narrow their content further, but never become
  modal graphic cards.

## Runtime content

The XML screens define stable destinations only. Settings, global tasks,
file-specific actions and installed applications remain dynamic catalogues.
Both prototypes consume the same adapters; only their component renderers
differ.

For dynamic switch and progress values, the implementation adapter updates the
ASCII text together with the bound subject. The XML uses explicit preview text
to demonstrate the compact state while retaining the real control bindings.

# 35. DDT's demos on HDMI: via a pull request to his repos

Date: 2026-10-07

Status: Superseded by [ADR-0036](0036-t22-wont-do.md)

## Context

T22: DDT's C64U bitstreams (github.com/0x444454: mandelbrUlt, look_ma_no_vic) drive only the analog video.

## Decision

Alex 2026-10-07: not for use in TRX64-Ultimate - the goal is a pull request to DDT's own repos that gives his designs our HDMI output ("ich will ihm dann per PR das HDMI device gerne hinstellen"); licence of every handed-over file checked first (his repos CC-BY 4.0); nothing goes to GitHub without Alex.

## Consequences

An agent was started (local clones under ~/Development/ddt, branch hdmi-out; CI harness branch t22-ddt-hdmi, never merged). Overturned the same day.

## Links

- [T22](../../docs/todo/T22.md)

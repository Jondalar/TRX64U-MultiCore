# 36. T22 (DDT's demos on HDMI) is won't do

Date: 2026-10-07

Status: Accepted (supersedes [ADR-0035](0035-ddt-demos-on-hdmi-via-pr.md))

## Context

Our HDMI code is GPL-3.0; DDT's repos are CC-BY 4.0, so offering it for a PR to him is not possible under CC-BY.

## Decision

Alex 2026-10-07: "T22 == won't do". Our HDMI code (GPL-3.0) is not offered under CC-BY 4.0 for a PR to DDT.

## Consequences

Nothing built, clones and branch removed. Side finding for the record: DDT's C64U.xdc HDMI P pins (B6/B5/B4/B3) do not pair with his N pins; the working pins are CLK D7, D0 B6, D1 D5, D2 B4, refclk 148.5 MHz.

## Links

- [T22](../../docs/todo/T22.md)

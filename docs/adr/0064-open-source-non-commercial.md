# 64. The project goes public: open source, non-commercial

Date: 2026-11-18

Status: Accepted (supersedes the "no public release" part of [ADR-0015](0015-no-100t-no-public-release.md); its 100T part stays)

## Context

ADR-0015 (2026-10-04) decided: no public release. Since then T120 built the GitHub copy: no ROM images in the tree or the history,
no internal hosts, the licence of every part in `PROVENANCE.md`, and the take-offline clause in the README. Release 0.2.0 is the
first release for the public repository (T221).

## Decision

Alex, 2026-11-18 (translated): "And then we change ADR-015, please: it becomes an open source project, non-commercial."

1. The GitHub repository becomes public.
2. Our own code stays GPL-3.0-or-later (`LICENSE`, `LICENSING.md`).
3. The project is non-commercial. We do not sell the firmware, the cores or the packages.
4. Third-party parts keep their licences (`PROVENANCE.md`). The take-offline clause of the README applies.

## Consequences

- All cores are released. This is the distribution decision of 2026-10-24 (Alex, translated: "We will definitely release our host
  and the cores too. So for the AMD parts as well: if we violate something there, we will gladly take it offline.").
  `PROVENANCE.md` names the origin and the licence of every part. A rights holder who sees a problem contacts us, and we remove the
  part concerned (`PROVENANCE.md` §2, `LICENSING.md`).
- The parts that T120 D2 lists (restrictive or missing licence texts: C4, C5, C7 to C10, H5, H11) do not block the release. They are
  the parts that a rights holder can ask about.
- T120 Q1 and Q3 are answered by this ADR.

## Links

- [T221](../specs/T221-semver-everywhere.md)
- [ADR-0015](0015-no-100t-no-public-release.md)

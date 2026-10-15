# OpenSpoof — Documentation index

This is the single entry point to OpenSpoof's documentation.
Every other doc links back here so you can navigate by topic.

> Status legend used across these docs:
> - **code done, syntax OK** — implementation present, passes
>   `-fsyntax-only` against real headers.
> - **awaiting device validation** — code is in place but has not
>   been run on a physical rooted device. See the verification
>   checklist in [ROADMAP.md](ROADMAP.md) § 5.

## By role

### I just want to use OpenSpoof
1. [README.md](../README.md) — overview, quick start, scope.
2. [SPEC.md](SPEC.md) § 3 (architecture) and § 7 (config schema).
3. WebUI guide: the on-device editor itself, opened from
   KernelSU / APatch.

### I want to build it
1. [README.md](../README.md) § Quick start.
2. [SPEC.md](SPEC.md) § 9 (build & CI).
3. `scripts/build.sh` / `scripts/build.ps1` — the scripts themselves
   are the canonical build reference.

### I want to understand the design
1. [SPEC.md](SPEC.md) — contract, architecture, scope, non-goals.
2. [ROADMAP.md](ROADMAP.md) — phases, gap analysis, verification.
3. [PHASE4.md](PHASE4.md) — Dobby native hooks in depth.

### I want to contribute code
1. [SPEC.md](SPEC.md) § 8 (repo layout) and § 10 (testing plan).
2. [ROADMAP.md](ROADMAP.md) § 5 (verification checklist).
3. [CHANGELOG.md](CHANGELOG.md) — record your change here.

### I want to audit the project
1. [SPEC.md](SPEC.md) § 2 (non-goals) and § 11 (risks & limits).
2. [ROADMAP.md](ROADMAP.md) § 1 (current status) and § 6 (known
   limitations).
3. [PHASE4.md](PHASE4.md) § 8 (risks of native hooks).

## Document map

| Doc                                | Purpose                                    | Audience       |
|------------------------------------|--------------------------------------------|----------------|
| [README.md](../README.md)          | Project pitch, quick start, scope          | Everyone       |
| [SPEC.md](SPEC.md)                 | Contract: what OpenSpoof is and isn't      | Users & devs   |
| [ROADMAP.md](ROADMAP.md)           | Phases, status, gap analysis, checklist    | Maintainers    |
| [PHASE4.md](PHASE4.md)             | Native hook engine (Dobby) details         | Hook authors   |
| [CHANGELOG.md](CHANGELOG.md)       | Release notes per version                  | Users & devs   |

## Document hierarchy

```
README.md
  └── "what is this and how do I try it"
        │
        ▼
docs/INDEX.md  ◀── you are here
        │
        ├── docs/SPEC.md
        │     "what is the contract"
        │       ├── architecture
        │       ├── config schema
        │       ├── non-goals
        │       └── testing plan
        │
        ├── docs/ROADMAP.md
        │     "where are we and what's left"
        │       ├── current status
        │       ├── gap vs SkySpoof / COPG
        │       └── verification checklist
        │
        ├── docs/PHASE4.md
        │     "how do the native hooks work"
        │       ├── engine choice
        │       ├── SELinux
        │       └── risks
        │
        └── docs/CHANGELOG.md
              "what changed in each release"
```

## What is **not** in this documentation

- **API reference** — there is no public C/C++ API; OpenSpoof is a
  Magisk module, not a library.
- **Per-ROM guides** — by design. The verification checklist in
  [ROADMAP.md](ROADMAP.md) is the closest thing; per-ROM notes
  belong in issues, not here.
- **Comparison with closed-source tools beyond the gap matrix** —
  the gap matrix in [ROADMAP.md](ROADMAP.md) § 2 is the limit.
- **Anti-cheat circumvention guides** — explicitly out of scope
  (see [SPEC.md](SPEC.md) § 2).

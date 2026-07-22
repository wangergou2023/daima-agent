# ADR-002: Use C11 as the Primary Language

- **Status**: Accepted
- **Date**: 2026-06

## Context

Initial planning explored TypeScript for rapid prototyping. After evaluating the target deployment environments (embedded Linux on MIPS/ARM, SPIFFS filesystem, resource-constrained devices), TypeScript was ruled out.

## Decision

Use **C11** with a Linux-kernel-style Kbuild system.

## Rationale

1. **Single binary** — no runtime dependency (no Node.js, no Python)
2. **Minimal footprint** — binary ~1.1MB, memory ~16MB
3. **Cross-compilation** — MIPS/ARM via ARCH= parameter
4. **Kbuild familiarity** — Makefile pattern matches Linux kernel development workflow
5. **Architecture clarity** — Bus/Driver/Device model maps naturally to C's module boundary

## Consequences

- Slower development iteration vs interpreted languages
- Manual memory management (mitigated by kmalloc/kfree wrappers)
- JSON handled via cJSON library
- Web UI as static files in spiffs_data/web/ (served by embedded HTTP server)

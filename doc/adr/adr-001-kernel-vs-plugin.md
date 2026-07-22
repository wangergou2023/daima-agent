# ADR-001: Build a New Agent Kernel Instead of a Host Plugin

- **Status**: Accepted
- **Date**: 2026-06-30

## 1. Context

The project goal is to build a dynamic agent organization framework centered on a **Boss / HR / Specialist** lifecycle.

Core requirements from the product direction:

1. **Boss is the single task entrypoint**
   - receives user requests
   - analyzes task requirements
   - routes to an existing specialist agent when possible
   - executes the task directly when no matching specialist exists

2. **HR is a background optimizer**
   - scans Boss execution transcripts
   - identifies repeated successful task patterns
   - distills new specialist agents from those patterns
   - updates the agent registry over time

3. **Specialist agents are dynamic system entities**
   - not only predefined prompt variants
   - have stable identity, explicit capability boundaries, and reusable definitions
   - may evolve as more transcripts become available

The project is also aligned with a **Bus / Driver / Device** architecture, where orchestration, runtime execution, memory, and registry responsibilities are intentionally separated.

A key architectural decision is required:

- Should this framework be implemented primarily as a **plugin/workflow layer** on top of an existing host such as OpenCode or Codex?
- Or should it be implemented as a **new Agent Kernel**, while reusing ideas and selected components from those systems?

## 2. Decision Drivers

The decision is driven by the following constraints:

1. **Dynamic agent lifecycle support**
   - The system must support creation, registration, lookup, and later evolution of specialist agents.

2. **Boss fallback execution**
   - Boss must be able to directly execute uncovered tasks, not only delegate.

3. **HR background processing**
   - HR must scan transcripts asynchronously or periodically, outside the immediate user request loop.

4. **Transcript as a first-class data source**
   - Structured transcripts are not logs only; they are core product data used for distillation.

5. **Long-term architectural control**
   - The system should not be tightly constrained by host runtime assumptions or undocumented extension points.

6. **Fast MVP path**
   - Early validation should remain practical without forcing full reinvention of commodity runtime features.

## 3. Considered Options

### Option A — Build as an OpenCode/Codex Plugin or Workflow Layer

Implement Boss/HR behavior as an extension on top of an existing host runtime.

Possible forms:
- plugin-based orchestration
- workflow layer
- external tooling around host sessions
- generated host agent definitions

#### Advantages
- faster MVP bootstrap
- reuse of existing runtime, tools, model integrations, and session handling
- lower initial engineering cost

#### Disadvantages
- core Boss/HR lifecycle depends on host extension boundaries
- dynamic specialist generation may rely on fragile or non-primary APIs
- HR background processing becomes unnatural or externalized
- host execution model may conflict with Boss fallback behavior
- long-term product boundaries become constrained by host assumptions

### Option B — Build a New Agent Kernel

Implement the framework as its own kernel, with first-class support for:
- Boss routing and fallback execution
- transcript storage
- HR distillation workflow
- specialist registry
- reusable runtime interfaces

Commodity capabilities may still be inspired by or wrapped from existing systems.

#### Advantages
- architecture matches the product model directly
- Boss/HR lifecycle becomes a first-class design primitive
- transcript and registry become native core subsystems
- long-term extensibility and control are much stronger
- avoids dependence on unstable host internals

#### Disadvantages
- higher initial implementation cost
- more system responsibilities must be owned directly
- requires explicit design of runtime contracts and module boundaries

### Option C — Hybrid Path: Host-Assisted MVP, Kernel as the Product Direction

Use an existing host temporarily for prototyping and validation, while keeping the target architecture as a standalone kernel.

Typical usage:
- validate transcript schema
- validate HR clustering/distillation workflow
- validate routing/fallback assumptions
- avoid committing product boundaries to the host platform

#### Advantages
- preserves speed for MVP learning
- reduces early implementation burden
- avoids confusing the prototype with the final architecture

#### Disadvantages
- requires discipline to prevent prototype-host coupling
- introduces some temporary duplication or migration work
- may tempt the project into over-investing in host-specific glue

## 4. Decision

**We will build this project as a new Agent Kernel.**

**A host-assisted prototype is allowed for MVP validation, but host plugin architecture is not the product boundary.**

## 5. Rationale

This decision is made because the defining value of the project is not “better prompts on top of an existing agent runtime.”  
The defining value is the **dynamic organizational lifecycle**:

- Boss receives and routes work
- Boss executes uncovered work directly
- transcripts are captured as structured evidence
- HR mines those transcripts
- HR distills new specialist agents
- future work is routed through the newly evolved organization

That lifecycle is the product itself.

A plugin model is acceptable when the product goal is:
- extending tool access
- adding workflows
- layering roles over an existing runtime
- packaging reusable prompts, skills, or commands

However, this project requires deeper control:

1. **Specialist agents must be first-class entities**
   - not just preconfigured prompt roles
   - not just static config artifacts

2. **HR must operate outside the foreground request loop**
   - it is a background optimizer, not a request-time helper

3. **Boss must be both router and executor**
   - not only an orchestrator that always delegates

4. **Transcript quality determines product quality**
   - transcript generation and storage must be owned deliberately from day one

5. **Bus / Driver / Device already implies kernel ownership**
   - the architecture assumes explicit control over contracts between orchestration, runtime, memory, and registry layers

Existing systems such as OpenCode or Codex remain valuable as references for:
- agent loop design
- tool integration
- session handling
- model routing
- plugin ergonomics
- memory patterns
- safety and sandboxing ideas

But they should be treated as **reference implementations or reusable execution-layer ideas**, not as the long-term system shell.

## 6. Consequences

### Positive Consequences

- Product architecture directly reflects the PRD
- Boss, HR, transcript store, and registry become first-class components
- Dynamic specialist generation is no longer a workaround
- Future evolution (multi-level orgs, agent growth, boundary refinement) remains possible
- Bus / Driver / Device can be kept clean and intentional

### Negative Consequences

- Higher up-front design burden
- More runtime infrastructure must be built or wrapped
- MVP may take longer than a pure host-plugin prototype
- The team must define stable interfaces early

### Engineering Consequences

The following items become mandatory architectural work:
- Bus event contracts
- Driver interfaces
- transcript schema
- agent definition schema
- registry semantics
- runtime abstraction boundaries

The following items can still be borrowed or wrapped:
- LLM provider integration
- tool adapters
- MCP integration
- sandbox approach
- execution loop patterns
- memory/indexing patterns

## 7. MVP Exception Policy

A host runtime may be used temporarily for prototyping **only if** all of the following are true:

1. The prototype is used to validate product assumptions, not to define final architecture.
2. Boss/HR logic is kept conceptually separate from host-specific plugin glue.
3. Transcript schemas and registry concepts remain owned by this project.
4. No host-specific undocumented behavior becomes a required product dependency.

### Allowed Prototype Uses
- testing Boss routing heuristics
- validating transcript structure
- validating HR clustering/distillation ideas
- demonstrating specialist reuse in a limited flow

### Not Allowed as Product Boundary
- defining specialists solely as host config artifacts
- tying HR behavior to host-only compaction/session internals
- assuming host plugin hooks are equivalent to kernel lifecycle support
- treating the host’s agent model as the source of truth for future architecture

### Extraction Trigger
The project must move fully to standalone kernel ownership once:
- transcript format stabilizes
- registry semantics stabilize
- Boss/HR lifecycle is validated
- host constraints begin shaping product behavior unnaturally

## 8. Rejected Alternatives

### Rejected: “Just build a plugin first and see later”
Rejected because it creates a strong risk that short-term convenience becomes accidental long-term architecture. For this project, that would place the product’s core lifecycle under another system’s extension model.

### Rejected: “Build everything from scratch immediately”
Rejected because not every subsystem is novel. Runtime and tooling layers may reuse proven patterns or wrapped implementations. Reinventing every commodity layer would slow validation unnecessarily.

## 9. Follow-Up Decisions Required

This ADR implies the following follow-up documents must be written next:

1. **ADR-002: Language Selection**
2. **Architecture Overview**
3. **Interface Contracts**
4. **Data Models**
5. **MVP Plan**

## 10. Summary

This project is fundamentally a **dynamic agent organization kernel**, not a host plugin product.

Therefore:

- **Product direction**: standalone Agent Kernel
- **Prototype policy**: host-assisted validation allowed
- **Architectural ownership**: Boss, HR, transcript, registry, and lifecycle remain first-class native concerns

---
name: design-squad
description: Use this skill when the user wants to delegate a task to the full design squad: Senior Game Designer, Senior UI/UX Designer, Senior 2D Texture Artist, Senior 3D Model Artist, and Senior Sound Artist.
---

# Design Squad

Delegate the given task to all design agents in parallel and consolidate their responses.

## Process

### Step 1 — Launch all design agents in parallel

Launch ALL of the following agents **simultaneously** using the Task tool, passing the user's task to each one:

| Agent | Subagent type | Expertise |
|---|---|---|
| Senior Game Designer | `gamedesign-lookandfeel` | Gameplay balance, traffic systems, economy simulation, city mechanics |
| Senior UI/UX Designer | `gamedesign-ux` | User interface design, player interaction flows, HUD, menus |
| Senior 2D Texture Artist | `graphics-artist-2d-texture` | Texture creation, art style, UV mapping, material design |
| Senior 3D Model Artist | `graphics-artist-3d-model` | 3D asset creation, model specs, polygon budgets, LOD design |
| Senior Sound Artist | `sound-artist-opensoftal` | Audio design, sound effects, music composition, audio asset requirements |

Each agent prompt should be:

> You are a [role title] working on AI Town, a 3D city simulator built with C++, Irrlicht, and OpenAL Soft. Your task: [USER'S TASK]. Apply your domain expertise fully. Provide concrete, actionable output — designs, specifications, recommendations, or analysis as appropriate. Be specific and thorough.

### Step 2 — Collect and present results

After all agents respond, present their outputs in clearly labelled sections:

```
=== DESIGN SQUAD RESULTS ===

--- Senior Game Designer ---
[output]

--- Senior UI/UX Designer ---
[output]

--- Senior 2D Texture Artist ---
[output]

--- Senior 3D Model Artist ---
[output]

--- Senior Sound Artist ---
[output]
```

### Step 3 — Synthesize

After presenting individual outputs, provide a brief synthesis:
- Note any cross-agent agreements or conflicts
- Highlight the most critical recommendations or decisions
- If agents made conflicting suggestions, reason about the best resolution and state a recommendation

## Rules

- All five agents must always be launched — never skip any
- All agents run in parallel to minimize latency
- Present each agent's full output without truncation
- If agents produce conflicting recommendations, surface the conflict explicitly rather than silently picking one

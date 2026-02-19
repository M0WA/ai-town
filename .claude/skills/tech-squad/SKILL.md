---
name: tech-squad
description: Use this skill when the user wants to delegate a task to the full technical squad: Senior GitHub Pipeline Engineer, Senior C++ Developer (Irrlicht), Senior C++ Developer (OpenAL Soft), and Senior C++ Test Engineer.
---

# Tech Squad

Delegate the given task to all technical agents in parallel and consolidate their responses.

## Process

### Step 1 — Launch all technical agents in parallel

Launch ALL of the following agents **simultaneously** using the Task tool, passing the user's task to each one:

| Agent | Subagent type | Expertise |
|---|---|---|
| Senior GitHub Pipeline Engineer | `cicd-dev-github` | GitHub Actions, CI/CD pipelines, build automation |
| Senior C++ Developer (Irrlicht) | `graphics-dev-irrlicht` | Irrlicht 3D engine, rendering pipeline, scene graph |
| Senior C++ Developer (OpenAL Soft) | `sound-dev-opensoftal` | OpenAL Soft, 3D spatial audio, audio engine integration |
| Senior C++ Test Engineer | `test-dev-cpp` | C++ unit/integration testing, Google Test, GMock, coverage |

Each agent prompt should be:

> You are a [role title] working on AI Town, a 3D city simulator built with C++, Irrlicht, and OpenAL Soft. Your task: [USER'S TASK]. Apply your domain expertise fully. Provide concrete, actionable output — code, designs, recommendations, or analysis as appropriate. Be specific and thorough.

### Step 2 — Collect and present results

After all agents respond, present their outputs in clearly labelled sections:

```
=== TECH SQUAD RESULTS ===

--- Senior GitHub Pipeline Engineer ---
[output]

--- Senior C++ Developer (Irrlicht) ---
[output]

--- Senior C++ Developer (OpenAL Soft) ---
[output]

--- Senior C++ Test Engineer ---
[output]
```

### Step 3 — Synthesize

After presenting individual outputs, provide a brief synthesis:
- Note any cross-agent agreements or conflicts
- Highlight the most critical recommendations or decisions
- If agents made conflicting suggestions, reason about the best resolution and state a recommendation

## Rules

- All four agents must always be launched — never skip any
- All agents run in parallel to minimize latency
- Present each agent's full output without truncation
- If agents produce conflicting recommendations, surface the conflict explicitly rather than silently picking one

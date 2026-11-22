# 0003: Engine: Unreal Engine 5.8, DCC: Blender

Status: Accepted
Date: 2026-11-22
Issue: none
Supersedes: none

## Context

The owner specified Unreal Engine and Blender up front. The owner has Unreal Engine 5.8.3 installed with experimental Model Context Protocol plugins, and Blender 4.4 and 5.1.

## Decision

The game is built with Unreal Engine 5.8 (project `Game/CombatSimulator.uproject`, C++ primary module `CombatSimulator`, EngineAssociation `5.8`). Blender is the content creation tool, exporting to Unreal. Unreal builds, packaging and Steam uploads run on the owner's PC, since Unreal cannot build in GitHub's cloud CI.

## Consequences

- Epic engine code and content may not be redistributed in the public repo.
- No GPL, LGPL or CC-BY-SA material may be combined with the engine (see 0004).
- Unreal MCP is experimental, so editor-only use is assumed (OD-010).
- Unreal cannot build in GitHub's cloud CI, so builds, packaging and Steam uploads run on the owner's PC.

## Alternatives

No alternatives were weighed; the owner chose the engine and the content tool up front.

## Links

- Open decisions: OD-009 (Blender 5.2 LTS), OD-010 (experimental Unreal MCP in shipped builds)

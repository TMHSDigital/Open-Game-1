# 0003: Engine: Unreal Engine 5.8, DCC: Blender

Status: Accepted
Date: 2026-11-22
Issue: none
Supersedes: none

## Context

The game is a physics sandbox for Steam on Windows. The owner has Unreal Engine 5.8.3 installed with experimental Model Context Protocol plugins, and Blender 4.4 and 5.1 with MCP tooling, which lets Claude drive both applications.

## Decision

The game is built with Unreal Engine 5.8 (project `Game/CombatSimulator.uproject`, C++ primary module `CombatSimulator`, EngineAssociation `5.8`). Blender is the content creation tool, exporting to Unreal. Unreal builds, packaging and Steam uploads run on the owner's PC, since Unreal cannot build in GitHub's cloud CI.

## Consequences

- Epic engine code and content may not be redistributed in the public repo; only project source and content are committed.
- No GPL, LGPL or CC-BY-SA material may be combined with the engine, which shapes the asset policy (0004).
- Unreal MCP is experimental, so editor-only use is assumed (OD-010).
- Cloud CI covers the Python checks only; build verification is local.

## Alternatives

- Unity or Godot: not installed, and no existing MCP tooling in the owner's setup.
- A custom engine: far more work than the game itself.
- Another DCC tool than Blender: Blender is free, scriptable and has the owner's MCP tooling.

## Links

- Open decisions: OD-009 (Blender 5.2 LTS), OD-010 (experimental Unreal MCP in shipped builds)

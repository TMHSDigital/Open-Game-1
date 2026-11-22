# Combat Simulator

Combat Simulator is a moddable physics sandbox game for Steam, built with Unreal Engine 5.8.

It is **built in public**: this repository holds the game source, the design decisions, the tooling and the devlog, and everything committed here is published.

## Follow along

- Devlog site: https://tmhsdigital.github.io/Open-Game-1/ (built from `docs/devlog/` by CI and published with GitHub Pages)
- Open questions: [docs/OPEN-DECISIONS.md](docs/OPEN-DECISIONS.md)
- Decision records: [docs/decisions/](docs/decisions/)
- Repo tooling (`og` command line): [docs/TOOLS.md](docs/TOOLS.md)
- Day-to-day workflows: [docs/WORKFLOWS.md](docs/WORKFLOWS.md)

## Layout

- `Game/` Unreal Engine project (C++ module `CombatSimulator`)
- `tools/` Python tooling (`og`), Unreal and Steam helper scripts, git hooks
- `docs/` decisions, devlog, milestones, capture index
- `.claude/skills/` project skills used with Claude Code

## License

Source-available, all rights reserved. The code is public for viewing and learning only; viewing or forking it grants no license to use, copy, modify or distribute it. Third-party assets keep their own licenses (see `CREDITS.md`). Mod terms will be published separately. Full text in [LICENSE](LICENSE).

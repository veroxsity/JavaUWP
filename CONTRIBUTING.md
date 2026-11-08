# Contributing

Bandit Launcher is built for legitimate Minecraft accounts. Contributions must preserve Microsoft/Xbox authentication and Minecraft entitlement checks.

Please do not submit changes that remove login, bypass ownership verification, add offline sessions, forge account identities, or make authentication optional. Pull requests that weaken account or entitlement enforcement will not be accepted.

Do not commit secrets such as signing keys, private certificates, refresh tokens, service credentials, or privileged API keys.

For normal development, keep changes scoped, follow the existing code style, and include logs or test notes for fixes that affect launch, downloads, profiles, graphics, controller input, or remote files.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the UWP host layout, launch flow, and loader module map.

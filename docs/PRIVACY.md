# Privacy

What Bandit Launcher does with your data, where it goes, and how long it stays there.
This covers the launcher itself. Minecraft, Mojang's services, Modrinth, GitHub and
anything else the launcher talks to have their own policies and are not covered here.

Accurate against `main` as of 14 September 2026.

## Who runs this

Bandit Launcher is made by veroxsity / BanditVault, a solo project based in the United
Kingdom. The telemetry server at `telemetry.banditvault.co.uk` is self-hosted on hardware
BanditVault controls. Nothing is sold, shared with advertisers, or passed to a third party.

## Signing in

Sign-in uses the Microsoft device code flow with the scopes `XboxLive.signin offline_access`.
You enter a code on a Microsoft page in your own browser, so the launcher never sees your
Microsoft password.

The launcher then exchanges tokens through Xbox Live, XSTS and Minecraft services, checks the
account owns Minecraft: Java Edition, and fetches the Java profile.

Held in memory for the session and passed to the Java process as launch arguments:

- your Minecraft username
- your Minecraft account UUID
- a Minecraft access token

Written to disk:

- the Microsoft refresh token, stored in the Windows Credential Locker under the resource
  `MinecraftJavaUWP.MicrosoftRefreshToken`. It is not written to LocalState and the launcher
  does not keep a plain text copy anywhere.

Because the username and UUID are passed to Minecraft as launch arguments, Minecraft's own
files and logs under your profile directory contain them. That is the Mojang client's
behaviour rather than the launcher's, but it is why those values appear if you read your own
logs.

None of this leaves the console except to Microsoft and Mojang. No account data is sent to a
BanditVault server at any point, including when telemetry is switched on.

Signing out removes the stored refresh token.

### Known issue: the application registration

The Microsoft application ID the launcher currently uses is Prism Launcher's public client ID
rather than one owned by BanditVault. The consent screen you see during sign-in therefore
names an application that is not this one. Replacing it with a Bandit-owned registration is
tracked work and this section will be removed when that lands.

## Telemetry

Off by default. The launcher asks once and sends nothing until you allow it.

Your answer is stored at `LocalState\telemetry\consent.txt` as `always` or `never`. Until you
answer, nothing is sent.

### Install ID

When telemetry is enabled the launcher generates a random GUID and stores it at
`LocalState\telemetry\install_id.txt`. It is not derived from your Microsoft account, your
gamertag, your console or any hardware identifier. It exists so that fourteen reports of the
same crash can be counted as fourteen installs rather than fourteen unknowns.

You can reset it from the launcher. Resetting generates a new GUID and clears anything queued
for sending. Rows already on the server keep the old ID, which no longer maps to your install.

### What is sent

Session beacons, to `/v1/session`, when a launch starts, when the first frame draws, when the
game becomes playable, when the console suspends, and when the game exits:

- install ID
- launch ID, a fresh random GUID for that launch
- which of those events it is
- launcher build number
- Minecraft version, loader, loader version
- mod set hash
- minutes elapsed in the session

Mod set, to `/v1/modset`, and only when the server replies that it has not seen that hash
before:

- install ID
- the mod set hash
- for each mod, the jar filename and, where known, its Modrinth project ID

Crash reports, to `/v1/report`, when a launch fails:

- install ID
- a crash fingerprint, which is a sixteen character hash
- launcher build, Minecraft version, loader, loader version, mod set hash
- which phase the crash happened in
- maximum heap size and heap in use at the crash

Stack traces, to `/v1/trace`, and only when the server replies that it holds no trace for that
fingerprint yet:

- install ID
- the fingerprint
- the Java exception class and its message
- up to sixteen stack frames
- for mixin failures, the target class, method, descriptor, owning mod and symbol

The crash screen shows you the exact payload before it is sent.

### What is never sent

- Crash report files, `hs_err_pid*.log`, `latest.log` or any other log file. Crash parsing
  happens on the console, so the server receives the result and never the input.
- Worlds, saves, screenshots, configs or any other file from your game directory.
- Your Microsoft account, Minecraft username, UUID, gamertag, or any token.
- Any of the above, while telemetry is off. The launcher still downloads the compatibility
  feed when telemetry is off, which is covered in its own section below, but it sends nothing.

### If a send fails

Anything that cannot be sent is written as JSON to `LocalState\telemetry\queue` and retried at
the next launch. The queue is capped at twenty files so a crash loop cannot fill storage.
Resetting the install ID clears it.

### Where it goes and how long it stays

Everything goes to `https://telemetry.banditvault.co.uk`, self-hosted by BanditVault behind a
Cloudflare tunnel. You can point it somewhere else with `endpoint.txt` in the telemetry folder
or the `BANDIT_TELEMETRY_ENDPOINT` environment variable.

A retention pass runs every six hours and thins the data out in stages.

- Per-launch session events are deleted after 7 days.
- Rate limit counters, which are keyed on install ID, are deleted after 7 days.
- Individual crash occurrences are deleted after 30 days. Before they go, they are folded into
  a daily count of how many distinct installs hit that crash. The count survives, the link to
  your install does not.
- Per-install daily usage rows are deleted after 90 days, folded the same way into daily
  totals that carry no install ID.

Two things are kept indefinitely, on purpose.

Crash fingerprints and their stored stack traces are kept until the bug is fixed and the row
is cleared by hand. A fingerprint row holds the exception, the message, the frames, the mixin
detail and a triage status. It does not hold an install ID, so it is not linked to you or to
anyone else. Only one trace is stored per fingerprint, from whichever install reported it
first.

Your install ID itself is kept, alongside the date it was first and last seen and the launcher
build it was on. Nothing else is attached to that row. If you reset your install ID the old row
stays but is orphaned, because nothing new will ever reference it.

If you want the rows for a particular install ID removed, email the address at the bottom with
that ID. You can read your own ID in the launcher.

## The compatibility feed

Once a day the launcher makes a conditional GET to `telemetry.banditvault.co.uk/v1/compat` to
download the list of mods known to crash on particular versions. This happens whether or not
telemetry is enabled, because it is a download rather than a report.

The request carries no install ID and nothing about you. Like any HTTP request it reveals your
IP address to the server, the same way downloading the game reveals it to Mojang. The response
is cached at `LocalState\telemetry\compat.json` and re-checked with an ETag, so on most days
the answer is a 304 with no body.

The ingest server does not log IP addresses. Its request logging is limited to method, path,
status code and duration, and it is switched off entirely outside development. The one place an
address is written to the container log is a failed login to the private admin portal, which is
not something the launcher ever touches. Cloudflare sits in front of the tunnel and keeps its
own edge logs under its own policy.

## Remote Files and the mouse relay

Both open HTTP servers on your local network. They are built for a home network you trust.

Remote Files listens on port 27632 on every network interface and serves the launcher's own
data under LocalState: profiles, worlds, mods, logs and crash reports. Access is gated by a
PIN, and that PIN appears in the URL. It is plain HTTP with no TLS, so anyone on the same
network who has the URL can read and write that data, and anyone able to watch traffic on that
network can read it in transit. It stays off unless you start it.

The mouse relay page listens on port 6090 on every network interface with no authentication,
and forwards pointer input to the launcher. It carries cursor movement and button presses, not
files and not account data. Anyone on your network can open it and move your cursor.

The companion relay apps for Windows, Android and iOS send pointer data over UDP on the local
network. They do not touch your account.

Do not expose either port to the internet.

## What the launcher downloads

- Minecraft client files, libraries and assets from Mojang's official endpoints, after
  ownership is verified. No game files are bundled in the package.
- Fabric, Forge and NeoForge loaders and their libraries, from their own distribution points.
- Mods and modpacks from Modrinth when you browse or install them. Those requests go to
  Modrinth and fall under Modrinth's own privacy policy.

## Your choices

- Answer Never at the telemetry prompt, or change it later in the launcher. Declining stops
  all reporting.
- Reset your install ID at any time to break the link between your console and rows already
  sent.
- Sign out to remove the stored refresh token.
- Leave Remote Files and the mouse relay stopped.
- Delete `LocalState\telemetry` to clear consent, install ID, queue and cached feed together.
- Email the address below with your install ID to have its rows removed from the server.

## Contact

info@banditvault.co.uk, for privacy questions, deletion requests, or anything else about this
document.

## Changes

This document is versioned in the repository. Material changes are noted in the release notes
for the build that carries them.

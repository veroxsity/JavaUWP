# Local trial build

This checkout starts with a local `LocalPlayer` profile without contacting
Microsoft/Xbox authentication or checking Minecraft entitlement. It does not
load, refresh, save, or clear Microsoft account tokens in this flow.

Select the **Player name** box above the main-menu preview to edit your name.
Use a mouse, or press Up from Play to focus the box and A/Enter to edit.
The Xbox keyboard and physical keyboard share the existing launcher text input.
Enter or closing the keyboard saves; B/Escape cancels. Names must contain
3-16 ASCII letters, digits, or underscores. Invalid input and save errors keep
your previous name. The choice is stored in the app's local state and reused
after restart. Changing the name changes the offline UUID and therefore the
local player identity; changing back restores the previous UUID.

The menu identifies the session as **Local trial**. **Reset session** recreates
the same local identity, so local player data keeps the same UUID. The launch
token is the literal placeholder `0`, not a valid Microsoft credential.

Game, loader, asset, and mod downloads still require network access. This is
not a network-isolated mode: existing update checks and telemetry settings
are unchanged. Servers and features requiring an authenticated session will
not work. Xbox execution and individual loader compatibility require testing
on the console.

The existing Microsoft authentication implementation remains in the source
for future restoration. The local entry point is `LoadLocalTrialAuthConfig`
in `MC.Xbox/auth/minecraft_auth.cpp`, used by `MC.Xbox/App.cpp`.

Build and package using the existing `build.ps1` workflow. These edits affect
source only until a new package is built and installed.

# FAQ
This doc answers Frequently Asked Questions in case you happen to be stuck.

## Will this hack my XBOX?
Bandit Launcher does not provide any way to hack the XBOX. All code runs natively on the console.

## What data does the launcher collect?
Signing in gives the launcher your Minecraft username, account UUID and an access token, which it needs to start the game. Those stay on the console. The only thing kept between sessions is your Microsoft refresh token, stored in the Windows Credential Locker so you don't have to sign in every time.

Crash and usage reporting is off until you switch it on. When it is on it sends version numbers, a mod set hash, a crash fingerprint and a stack trace, never your account details and never your log files. The full breakdown, including what the Remote Files and mouse relay servers expose on your network, is in [PRIVACY.md](PRIVACY.md).

## How do I turn crash reporting off?
Answer Never when the launcher asks, or change it later in the launcher. You can also reset your install ID from there, which generates a new random one and clears anything still waiting to send.

## Do I need a Java account?
Bandit Launcher requires a valid copy of Minecraft: Java Edition. The launcher does not condone piracy or redistribution of the game, but rather only makes it playable to legal users.

## My Minecraft account won't sign in!
If you have Minecraft through Game Pass, you need to create an account on minecraft.net and set a player name.

## How do I use my mouse?
On the (nightly page)[https://github.com/veroxsity/JavaUWP/tags], under mouse-relay-nightly, you can install an .exe if you want to use your Windows PC as a mouse, an APK if you want to use an Android, and an IPA if you want to use IPhone (which requires sideloading). You cannot plug your mouse directly into the XBOX, as Microsoft only allows you to use it on the Developer Mode Home.

## Is the mouse relay laggy?
The mouse relay does not lag unless you have a bad computer or *really* bad wifi. In other words, if it does lag, look into better wifi or a better device to use the relay.

## How does multiplayer work?
You don't need a Game Pass subscription or anything else to play multiplayer. You can hop on any server you wish (ideally with mouse relay and a keyboard).

## I tried to add Controlify (or another controller mod) to my instance, and it didn't work. What happened?
Before adding a different controller mod, click on the profile you're modifying and turn Bandit Controller off. That will prevent your desired controller mod from deleting itself when the game launches.

## How can I change the RAM allocation?
Unfortunately, you can't allocate more than 3-4 GB of RAM, as this is the max achievable by an XBOX in Developer Mode.

## How do I upload worlds?
Open Bandit Launcher, start Remote Files, and go to that link and upload your world to the portal.

Remote Files is plain HTTP on your local network, and the PIN sits in the URL, so treat it as open to anyone on the same wifi. Stop it when you're done, and never forward that port through your router.

## How do I check crash reports?
Open Bandit Launcher, start Remote Files, and go to the crash reports directory.

## Is this vibecoded?
Bandit Launcher is not vibecoded. However, certain comments have been as they're hard to write. The agent files have been added specifically to prevent forks from removing the authentication above.

## Other launchers?
As of 09/03/2026, *no* other launchers exist that haven't been mostly vibecoded. Please use them at your own risk. (As a reminder, vibecoded projects *are* unsafe to your devices. Also, obtain actual evidence of AI before accusing other people of vibecoding their projects. All launchers created after this date are unknown.)
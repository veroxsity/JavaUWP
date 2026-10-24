# Translating Bandit Launcher

The launcher can run in other languages. Each language is one text file, so adding one does not need any programming.

## Add a language

1. Copy `MC.Xbox/lang/en.json` and rename the copy after the language, for example `es.json` for Spanish or `pt-BR.json` for Brazilian Portuguese.
2. Change the `_name` line to the language's own name, such as `Español`. This is what Settings shows.
3. Translate the text on the right of each line. Leave the text on the left alone, the launcher uses it to find your translation.
4. Save the file as UTF-8.

Any line you leave out, or leave empty, shows in English. A half finished translation is fine to use and to send in.

## Things to keep as they are

- `{0}`, `{1}` and so on are filled in with names, numbers and versions. Move them wherever your language needs them, but keep them.
- `\n` is a line break.
- Names such as Minecraft, Modrinth, CurseForge, Fabric, Forge and NeoForge stay as they are.

## Try it on your Xbox

You do not need a new build to test a file.

1. Open Remote Files from the launcher's main menu and go to its web page on a PC or phone.
2. Upload your file into the `lang` folder.
3. Close and reopen the launcher, then choose your language in Settings under Language.

A file in `lang` replaces the one that came with the launcher if they share a name, so you can keep editing and reloading.

The launcher picks your language on its own the first time if the console is set to a language that has a file. Settings can change it at any time.

## Sending it in

Open a pull request with your file in `MC.Xbox/lang`, or send it in the project's Discord. When the English text changes in a later version, the changed line needs translating again. Running `scripts/update-translations.ps1 -Fill` adds the new lines to your file in English, so what is left is easy to spot.

## What is translated so far

Everything you read in the launcher: the main menu, Settings, sign in and its error messages, the profile and mod pages, downloads and imports, the text shown while Minecraft starts, the crash screen, the new version notice and the What's new page.

The Remote Files page on a PC or phone is translated too. It follows the language chosen in Settings, so reload the page after changing it.

A few things stay in English on purpose. Text written to the log files and crash reports is for people fixing bugs. Names, descriptions and changelogs that come from Modrinth, CurseForge or other websites are shown as their authors wrote them. The comments inside `jvm_args.txt` and `mesa_env.txt` are left alone as well.

Languages written with Latin, Cyrillic or Greek letters should work as they are. Others depend on the console having a suitable font, so please check how yours looks before sending it.

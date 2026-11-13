# Themes

The launcher comes with a few colour themes. Pick one in Settings under Theme. Each press moves to the next one, and the screen changes straight away.

| Theme | Look |
| --- | --- |
| Bandit | The default. Dark grey with a tan accent |
| Midnight | Dark blue with a light blue accent |
| Moss | Dark green with a lime accent |
| Ocean | Dark teal with a turquoise accent |
| Berry | Dark plum with a pink accent |
| Ember | Dark brown with an orange accent |
| Daylight | Light, with dark text and a brown accent |

## Make your own

A theme is one small text file. You only list the colours you want to change, anything you leave out comes from the theme you start from.

```json
{
  "name": "Cherry",
  "base": "midnight",
  "background": "#140C10",
  "surface": "#22161C",
  "raised": "#36242D",
  "accent": "#FF6F91",
  "mutedText": "#B59CA6"
}
```

| Line | What it changes |
| --- | --- |
| `name` | What Settings shows. Leave it out and the file name is used |
| `base` | The theme to start from: `bandit`, `midnight`, `moss`, `ocean`, `berry`, `ember` or `daylight`. Bandit if you leave it out |
| `background` | Behind everything |
| `surface` | Cards, panels and settings rows |
| `raised` | Buttons, and the row you are on |
| `text` | Normal text. The row your controller is on is filled with this colour |
| `mutedText` | Hints and smaller text |
| `accent` | The Play button, switches that are on, and the tab you are on |
| `success` | Things that worked, like the green dot in Remote Files |
| `danger` | Errors and the delete button |
| `focusText` | Text on top of the `text` and `accent` colours. If you leave it out the launcher picks black or white for you |

Colours are written as `#RRGGBB`, like `#FF6F91`. The short form `#F69` works too.

If the background is a light colour, the launcher treats the theme as a light one and lightens the boxes it draws over pictures so dark text stays readable.

## Try it on your Xbox

1. Open Remote Files from the launcher's main menu and go to its web page on a PC or phone.
2. Open the Themes folder and upload your file. It has to end in `.json`.
3. Open Settings on the launcher. Your theme is in the list after the built in ones.

You can keep editing the file and uploading it again. Leave Settings and open it again to see the change. A colour the launcher cannot read is skipped, and the one from `base` is used instead.

[`themes/cherry.json`](themes/cherry.json) is a finished example you can start from.

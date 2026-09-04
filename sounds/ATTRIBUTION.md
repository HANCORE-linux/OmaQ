# Sound attribution

## Call ringing tone

`phone.oga` is a transcoded call-progress tone used only while an incoming or outgoing direct call is ringing.

- Source: [bruit tonalité du telephone](https://pixabay.com/sound-effects/film-special-effects-bruit-tonalit%C3%A9-du-telephone-223780/)
- Creator: [u_bfmec9l9lj](https://pixabay.com/users/u_bfmec9l9lj-44888647/)
- Source notice: AI generated; published July 12, 2024
- License: [Pixabay Content License](PIXABAY-CONTENT-LICENSE.md)
- Original MP3 SHA-256: `a51b326f6b4b721351ea63010b5bfe230b336dcbcf879fcb7c5f924b9d4f8975`
- OmaQ `phone.oga` SHA-256: `84eb3dd0376dab5fd6cb54dca8d11e8576bc10e42809007e814a1e7474972015`
- Transformation: decoded, metadata removed, and encoded as 48 kHz stereo Ogg Vorbis
- Retrieved: August 25, 2026

## UHOH notification

`sounds/icq-message.mp3` is a transcoded derivative of the ICQ Desktop incoming-message sound. ICQ identifies the source only; OmaQ does not claim ICQ endorsement or trademark rights.

- Source: [`products/icq/app/resources/sounds/incoming.wav`](https://github.com/mail-ru-im/im-desktop/blob/78924d804fc38a5746a073d5bdb71c1c4cc97780/products/icq/app/resources/sounds/incoming.wav)
- Source commit: `78924d804fc38a5746a073d5bdb71c1c4cc97780`
- Copyright: Copyright (C) 2016 ICQ LLC (Mail.Ru Group)
- License: Apache License 2.0 (`LICENSES/Apache-2.0.txt`)
- Upstream notice: `LICENSES/ICQ-NOTICE.md`
- Original WAV SHA-256: `6060dfb8fc8fdc1b58bd9482f57c491a3b73a61f4289dbc8d2b5c7d4d54f406f`
- OmaQ `icq-message.mp3` SHA-256: `14dcb321bb71e37bdd1cf7a9e2b3b3fbcf759e2043eeff1ad69885c13c244cf1`
- Transformation: transcoded from 44.1 kHz stereo 16-bit PCM to 48 kHz stereo MP3, with leading and trailing silence

## Notification presets

The notification presets below are unmodified audio files from KDE's Ocean Sound Theme. The `qq.oga` and `msn.oga` compatibility filenames are presented as `PING` and `MAIL` in OmaQ.

- Source: https://github.com/KDE/ocean-sound-theme
- Source commit: `13ad78d18e844d0b0458ca1d71aa692ea093c845`
- Author: Guilherme Marçal Silva `<guimarcalsilva@gmail.com>`
- License: CC BY-SA 4.0 (`LICENSES/CC-BY-SA-4.0.txt`)

| OmaQ file | Ocean source file |
|---|---|
| `qq.oga` | `ocean/stereo/audio-volume-change.oga` |
| `msn.oga` | `ocean/stereo/message-new-email.oga` |
| `aurora.oga` | `ocean/stereo/completion-success.oga` |
| `glow.oga` | `ocean/stereo/power-plug.oga` |

## Short notification sounds

The following presets are derived from openly licensed source recordings on Wikimedia Commons. OmaQ trims them to notification length, converts them to 48 kHz stereo PCM, adjusts gain without clipping, and adds short fades. `knock.wav` also uses a short echo to preserve the three-knock decay after trimming.

| OmaQ file | Source and author | License | Original SHA-256 |
|---|---|---|---|
| `click.wav` | [Karabiner click 02](https://commons.wikimedia.org/wiki/File:457456_rudmer-rotteveel_karabiner-click-02.wav), Rudmer Rotteveel | CC0 1.0 (`LICENSES/CC0-1.0.txt`) | `0f80a63afb93490fce6cf7eac0a672123f735fbe227c68a94659145dedbd354a` |
| `knock.wav` | [Knock on door](https://commons.wikimedia.org/wiki/File:Knock_on_door.wav), Asud12 | CC0 1.0 (`LICENSES/CC0-1.0.txt`) | `c2d18abf40fac664b4e7e09ae811e2c7d9c78c24d44457787e8c33f58e3c1343` |

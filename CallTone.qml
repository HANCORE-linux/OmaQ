pragma Singleton

import QtQuick
import QtMultimedia

QtObject {
  id: root

  property int ownerSequence: 0
  property var requests: ({})
  readonly property bool playing: player.playing
  property MediaPlayer player: MediaPlayer {
    source: Qt.resolvedUrl("sounds/phone.oga")
    loops: MediaPlayer.Infinite
    audioOutput: AudioOutput { volume: 0.8 }
  }

  function acquireOwner() {
    root.ownerSequence = root.ownerSequence + 1
    return "tone-owner-" + root.ownerSequence
  }

  function stopAll() {
    root.requests = ({})
    player.stop()
  }

  function setRequested(owner, requested) {
    var key = String(owner || "")
    if (!key)
      return
    var next = ({})
    var active = false
    for (var current in root.requests)
      if (current !== key && root.requests[current])
        next[current] = true
    if (requested)
      next[key] = true
    for (var candidate in next)
      if (next[candidate]) {
        active = true
        break
      }
    root.requests = next
    if (active) {
      if (!player.playing)
        player.play()
    } else {
      player.stop()
    }
  }
}

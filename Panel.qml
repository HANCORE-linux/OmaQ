import QtQuick
import qs.Ui
import qs.Commons

Panel {
  id: root
  moduleName: "hancore.omaq"
  ipcTarget: "hancore.omaq"

  Service {
    id: omaq
    settings: root.settings
  }

  implicitWidth: 28
  implicitHeight: 28

  Text {
    anchors.centerIn: parent
    text: omaq.unreadCount > 0 ? String(omaq.unreadCount) : "Q"
    color: bar ? bar.barForeground : Color.foreground
    font.pixelSize: 12
  }
}

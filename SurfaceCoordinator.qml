pragma Singleton

import QtQuick

QtObject {
  id: coordinator

  property var hosts: []
  property var owner: null
  property bool demoOpen: false
  property bool pendingDemoOpen: false
  property string pendingConversation: ""
  property string pendingName: ""

  function registerHost(host) {
    if (!host || coordinator.hosts.indexOf(host) !== -1)
      return
    var next = coordinator.hosts.slice()
    next.push(host)
    coordinator.hosts = next
    coordinator.selectOwner()
  }

  function unregisterHost(host) {
    var next = []
    for (var i = 0; i < coordinator.hosts.length; i++)
      if (coordinator.hosts[i] && coordinator.hosts[i] !== host)
        next.push(coordinator.hosts[i])
    coordinator.hosts = next
    if (coordinator.owner === host)
      coordinator.owner = null
    coordinator.selectOwner()
  }

  function selectOwner() {
    if (coordinator.owner || coordinator.hosts.length === 0)
      return
    coordinator.owner = coordinator.hosts[0]
    Qt.callLater(coordinator.deliverPending)
  }

  function requestChat(conversation, name) {
    var key = String(conversation || "")
    if (!key)
      return
    coordinator.pendingConversation = key
    coordinator.pendingName = String(name || "")
    coordinator.deliverPending()
  }

  function deliverPending() {
    var host = coordinator.owner
    var conversation = coordinator.pendingConversation
    if (!host || !conversation || typeof host.acceptOpenRequest !== "function")
      return
    var name = coordinator.pendingName
    coordinator.pendingConversation = ""
    coordinator.pendingName = ""
    host.acceptOpenRequest(conversation, name)
  }

  function queueDemo() {
    coordinator.pendingDemoOpen = true
  }

  function deliverPendingDemo() {
    if (!coordinator.pendingDemoOpen)
      return
    coordinator.pendingDemoOpen = false
    coordinator.demoOpen = true
  }

  function openDemo() {
    coordinator.pendingDemoOpen = false
    coordinator.demoOpen = true
  }

  function closeDemo() {
    coordinator.pendingDemoOpen = false
    coordinator.demoOpen = false
  }
}

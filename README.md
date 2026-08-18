# OmaQ

Lightweight chat for Omarchy Quattro. Tox underneath, invite by link or QR, no account. Direct messages first; groups later on the same conversation and invite modules.

This directory is the project root. Do not copy it into `~/.config/omarchy/plugins/` until a reviewed install is announced.

**Name:** `OmaQ`  
**Plugin id:** `hancore.omaq`  
**Remote:** private `https://github.com/HANCORE-linux/OmaQ` — only the owner may make it public.  
**How we build:** [`docs/PLAN.md`](docs/PLAN.md) — start at **Architecture law**.

A phase is done only when `make verify-N` exits 0 and `docs/stages/0N-….md` exists. After that: commit and push. AUR is phase 7.

# Notes

Short dated findings, newest first. Status lives in README.md.

## 2026-11-04
- Reviewed branches. `panel-handoff`: patches apply and build, installed for testing (backups in /var/lib/*.before-handoff), boot test pending. `sensord`: reads stock partitions read-only, implements the `sns_registry` interface hexagonrpcd lacked; untested. Both add a patch 0040: renumber on merge.
- CLAUDE.md added. Optional modules come from the `gts7xl` pacman repo (`modules-repo/`).

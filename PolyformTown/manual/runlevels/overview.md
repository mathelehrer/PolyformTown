# Runlevels Overview

This project uses staged runlevels. Each stage has separate trust
status, commands, and validation requirements.

- RL0 initialization is relatively stable and well tested.
- RL0 refine introduces naming and parameter semantics to clean up.
- RL1 generation is active and needs careful audit before
  downstream use.

See `boot.md` and `data-trust.md` for operational rules.

# Fixed `MAX_PATH` buffer exemptions

No exemptions are currently approved.

## Exception contract

An exception is permitted only when an API contract makes a fixed caller-owned
buffer unavoidable and a compatibility adapter cannot yet be introduced. The
declaration must carry a trailing `MAX_PATH-RATCHET-EXEMPT: ID` marker, and
this register must contain a matching entry:

```markdown
### ID
- File: `src/example.cpp`
- Reason: The exact API contract requiring this buffer.
- Removal: The migration issue or concrete condition that removes it.
```

Exemptions are reviewed debt, never a mechanism for new ordinary path handling.

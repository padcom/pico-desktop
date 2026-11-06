# Architecture decision records

One file per decision: context, decision, consequences. **Accepted** decisions came out of the
design rounds (PROJECT_DESCRIPTION.md §11); **Proposed** ones are working defaults that an
experiment in the named phase confirms or replaces. Superseding a decision means a new ADR, not
editing the old one's decision.

| ADR | Decision | Status | Decided by |
|---|---|---|---|
| [0001](0001-separate-cef-host-and-pico-process.md) | A C++ CEF host and a separate Pico process | Accepted | design rounds 1–8 |
| [0002](0002-cbor-rpc-event-model.md) | CBOR envelopes; typed string-named RPC, events, managed subscriptions | Accepted (encoding details: F2) | design rounds 1–8 |
| [0003](0003-app-scheme-and-origin.md) | The trusted origin is `https://<app-id>.localhost`, served by the host | Accepted | F1 experiment |
| [0004](0004-host-pico-ipc-transport.md) | Length-prefixed stdin/stdout pipes between host and Pico | Accepted | F2 benchmark |
| [0005](0005-schema-format.md) | Contracts are written in CDDL | Accepted | F2 experiment |
| [0006](0006-six-target-artifacts.md) | Six native targets; portable bundles; Linux AppImage | Accepted | design rounds 1–8 |

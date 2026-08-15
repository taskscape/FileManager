# Reliability program

This is the operating record for the stability work described in `refactoring.md`.
It converts release evidence and field incidents into reviewed, measurable work.

## Objectives and owners

| Objective | Measure | Target | Accountable role | Evidence | Review cadence |
| --- | --- | --- | --- | --- | --- |
| Prevent native-operation data loss | Confirmed data-loss incidents per release | 0 | Core operations maintainer | Incident register and operation-recovery journal | Every release |
| Keep sessions responsive | Hang-free sessions | at least 99.95% over a rolling 30 days | Desktop runtime maintainer | Crash/timeout diagnostics and support incidents | Monthly |
| Keep releases safe | Release escapes requiring a hotfix | 0 | Release manager | Release issue labels and rollback/canary record | Every release |
| Recover interrupted operations | Reconciliation success for recoverable journals | at least 99.9% | Core operations maintainer | Recovery reports and fault-injection results | Monthly |
| Detect regressions before release | Required CI checks completed without skip | 100% | Build and test maintainer | GitHub Actions release-gate run | Every release |
| Reduce parser exposure | Untrusted parser crashes escaping the broker | 0 | Plug-in/platform maintainer | Fuzz corpus regressions and broker telemetry | Monthly |

## Required records

- Open one `reliability`-labelled issue for every production incident, failed canary, release-gate escape, or recovery failure within one business day.
- Record the affected build commit, product version, operation or parser boundary, customer impact, containment, owner, and target remediation release.
- Link a deterministic regression test or explain why an external fixture is required; the release manager cannot close the issue without that evidence.
- Review the metrics table and unresolved reliability issues at each release decision. A target miss needs an explicit exception approved by the accountable role and release manager.

## Delivery stages

| Stage | Exit evidence |
| --- | --- |
| Characterize | Native/UI behavior and fault matrix are executable; release gate rejects skipped required tests. |
| Harden | Parser/process boundaries, PE mitigation audit, retries, lock rules, and transactional commits have executable coverage. |
| Operate | Symbolization, canary/rollback, continuous fuzzing, field-incident metrics, and recovery results feed the next release. |

The current release authority is the gated GitHub Actions workflow. It must attach its test, installer, verifier, PE-hardening, and private-symbol artifacts to the associated release record.

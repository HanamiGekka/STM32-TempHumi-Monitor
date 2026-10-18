# Verification tools

[Build/test guide](../docs/development/BUILD.md). Host probes use synthetic/replay fixtures and never imply physical accuracy. Hardware runners require an explicit selected port and expected firmware. The optional installer hardware harness reads TEMPHUMI_TEST_EXPECTED_UID_SHA256 supplied by the operator; no project board identifier is embedded in this public source. Use new output directories and preserve failures.

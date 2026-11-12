#!/bin/bash
# GitHub's release-download redirect now carries a ~4.2 KB Content-Security-Policy
# header, over the SDK's 4096-byte header-line cap, so OTA downloads failed with
# status -1. Raise the line cap and the total header budget (pinned SDK copy).
set -e
F="$(dirname "$0")/../freeink-sdk/libs/network/SecureNet/include/SecureHttpClient.h"
sed -i 's/static constexpr size_t MAX_LINE = 4096;/static constexpr size_t MAX_LINE = 8192;/' "$F"
sed -i 's/if (headerBytes > 8192 || _responseHeaders.size() >= 64)/if (headerBytes > 16384 || _responseHeaders.size() >= 64)/' "$F"
grep -q "MAX_LINE = 8192" "$F" && grep -q "headerBytes > 16384" "$F"

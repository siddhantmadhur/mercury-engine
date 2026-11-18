#!/bin/bash

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

read -r -d '' LICENSE_TXT <<EOM
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
EOM

FILE_NAME=$1

# Do not use `echo -e` here: it interprets backslash escapes in the file being
# prefixed and corrupts C++ strings such as "\n".
tmp="$(mktemp)"
{
    printf '%s\n\n' "$LICENSE_TXT"
    cat "$FILE_NAME"
} >"$tmp"
mv "$tmp" "$FILE_NAME"

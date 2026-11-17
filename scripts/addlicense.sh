#!/bin/bash

read -r -d '' LICENSE_TXT <<EOM
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
EOM

FILE_NAME=$1

echo -e "$LICENSE_TXT\n\n$(cat $FILE_NAME)" >$FILE_NAME

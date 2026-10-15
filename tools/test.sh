#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/huaweirealm-tests.XXXXXX")
cc=${CC:-cc}
client="$repo_root/Client"

printf 'Test work directory: %s\n' "$test_dir"
cd "$test_dir"
python3 -B -m unittest discover -s "$client/tests" -p 'test_*.py' -v
python3 -B "$client/tests/test_login_failure_feedback.py"

"$cc" -std=c99 -Wall -Wextra -pthread \
    "$repo_root/Server/tests/test_server_gui_console.c" -o server_console
./server_console
printf 'PASS server_console\n'

"$cc" -std=c99 -Wall -Wextra -pthread \
    "$client/tests/test_chat_network.c" \
    "$client/mycode/chat_network.c" "$client/mycode/chat_transfer.c" -o network_parse
./network_parse
printf 'PASS network_parse\n'

"$cc" -std=c99 -Wall -Wextra \
    "$client/tests/test_chat_transfer.c" "$client/mycode/chat_transfer.c" -o transfer_sha256
./transfer_sha256
printf 'PASS transfer_sha256\n'

for test_name in test_chat_network_stream test_chat_network_upload_notice test_chat_network_timeout; do
    "$cc" -std=c99 -Wall -Wextra -pthread \
        "$client/tests/$test_name.c" "$client/mycode/chat_transfer.c" -o "$test_name"
    timeout 15 "./$test_name"
    printf 'PASS %s\n' "$test_name"
done

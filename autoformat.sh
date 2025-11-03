#!/bin/bash
echo "Formatting code ..."
find include -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) -exec clang-format -i {} +
find src -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) -exec clang-format -i {} +
find test -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) -exec clang-format -i {} +

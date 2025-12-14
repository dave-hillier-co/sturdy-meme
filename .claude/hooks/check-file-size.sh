#!/bin/bash

# Hook to check if file size has increased by more than 1000 lines
# Returns a system message prompting refactoring when file growth is too large

# Read JSON input from stdin
input=$(cat)

# Extract file path from tool input
file_path=$(echo "$input" | jq -r '.tool_input.file_path // ""')

# Validate we have a file path
if [[ -z "$file_path" || "$file_path" == "null" ]]; then
  exit 0
fi

# Count lines in the file
if [[ -f "$file_path" ]]; then
  current_lines=$(wc -l < "$file_path" | tr -d ' ')

  # Get the previous line count from git (HEAD version)
  previous_lines=$(git show HEAD:"$file_path" 2>/dev/null | wc -l | tr -d ' ')

  # If file didn't exist before, previous_lines will be 0
  if [[ -z "$previous_lines" ]]; then
    previous_lines=0
  fi

  # Calculate the increase
  increase=$((current_lines - previous_lines))

  if (( increase > 1000 )); then
    # Get just the filename for cleaner output
    filename=$(basename "$file_path")

    # Return structured feedback to trigger refactoring
    jq -n \
      --arg file "$filename" \
      --arg path "$file_path" \
      --arg prev "$previous_lines" \
      --arg curr "$current_lines" \
      --arg inc "$increase" \
      '{
        "decision": "block",
        "reason": "File \($file) has grown by \($inc) lines (\($prev) → \($curr)). This exceeds the 1000 line increase limit. Please refactor this file into smaller components before continuing."
      }'
  fi
fi

exit 0

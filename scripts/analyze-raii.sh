#!/bin/bash
# RAII Ratchet
# Counts, per header under src/, two things the codebase is migrating away from:
#   (a) public lifecycle methods: init / initialize / cleanup / destroy / shutdown
#   (b) raw Vulkan / VMA handle data members (vk::Buffer, vk::Image, VmaAllocation, ...)
#       that are not wrapped in an RAII type
#
# The totals are ratcheted against scripts/raii-baseline.txt: they may only go down.
#
# Usage: ./scripts/analyze-raii.sh [--verbose] [--check] [--update]
#   --verbose  Print every matching line
#   --check    Exit nonzero if either total exceeds the baseline (for CI)
#   --update   Rewrite the baseline with the current totals
#
# Runs on macOS /bin/bash 3.2 (no associative arrays, no mapfile).

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SRC_DIR="$PROJECT_ROOT/src"
BASELINE="$SCRIPT_DIR/raii-baseline.txt"

if [[ -t 1 ]]; then
    RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; CYAN='\033[0;36m'; BOLD='\033[1m'; NC='\033[0m'
else
    RED=''; GREEN=''; YELLOW=''; CYAN=''; BOLD=''; NC=''
fi

VERBOSE=false
CHECK=false
UPDATE=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --verbose|-v) VERBOSE=true; shift ;;
        --check|-c)   CHECK=true; shift ;;
        --update|-u)  UPDATE=true; shift ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [--verbose] [--check] [--update]"
            exit 1
            ;;
    esac
done

# Scan one header. Emits lines of the form:
#   M <line-number> <text>   public lifecycle method declaration
#   H <line-number> <text>   raw handle data member
scan_header() {
    awk '
    BEGIN {
        in_block = 0
        access = "public"
        handle_types = "(Pipeline|PipelineLayout|DescriptorSetLayout|Sampler|Image|ImageView|Buffer|RenderPass|Framebuffer|DescriptorPool|CommandPool|Semaphore|Fence)"
        handle_re = "^[[:space:]]*((vk::|Vk)" handle_types "|VmaAllocation)[[:space:]]+[a-zA-Z_][a-zA-Z0-9_]*[[:space:]]*(=|;|\\{|\\[)"
        method_re = "^[[:space:]]*((static|virtual|inline|explicit|constexpr)[[:space:]]+)*[A-Za-z_][A-Za-z0-9_:<>]*[[:space:]]+(init|initialize|cleanup|destroy|shutdown)[[:space:]]*\\("
    }
    {
        line = $0
        # Strip block comments (possibly spanning lines) and line comments.
        out = ""
        while (length(line) > 0) {
            if (in_block) {
                e = index(line, "*/")
                if (e == 0) { line = ""; break }
                line = substr(line, e + 2)
                in_block = 0
            } else {
                s = index(line, "/*")
                l = index(line, "//")
                if (l > 0 && (s == 0 || l < s)) { out = out substr(line, 1, l - 1); line = ""; break }
                if (s == 0) { out = out line; line = ""; break }
                out = out substr(line, 1, s - 1)
                line = substr(line, s + 2)
                in_block = 1
            }
        }
        line = out
        if (line ~ /^[[:space:]]*$/) next

        # Track access specifiers. struct defaults to public, class to private.
        if (line ~ /^[[:space:]]*(public|private|protected)[[:space:]]*:/) {
            if (line ~ /public/) access = "public"
            else if (line ~ /protected/) access = "protected"
            else access = "private"
            next
        }
        if (line ~ /^[[:space:]]*(template[[:space:]]*<[^>]*>[[:space:]]*)?(class|struct)[[:space:]]+[A-Za-z_]/ \
            && line !~ /;[[:space:]]*$/ && line !~ /enum/ && line !~ /friend/) {
            if (line ~ /^[[:space:]]*(template[[:space:]]*<[^>]*>[[:space:]]*)?class[[:space:]]/) access = "private"
            else access = "public"
        }

        # (a) public lifecycle method declarations
        if (access == "public" && line ~ method_re) {
            if (line !~ /destroy[[:space:]]*\([[:space:]]*Entity[[:space:]]/ \
                && line !~ /destroy[[:space:]]*\([[:space:]]*TransformHandle[[:space:]]/ \
                && line !~ /^[[:space:]]*return[[:space:]]/) {
                print "M " FNR " " line
            }
        }

        # (b) raw handle data members. Reject anything that looks like a parameter
        # (trailing comma or closing paren) or a local that is immediately used.
        if (line ~ handle_re && line !~ /\)/ && line !~ /,[[:space:]]*$/ && line !~ /^[[:space:]]*(return|using|typedef)[[:space:]]/) {
            print "H " FNR " " line
        }
    }' "$1"
}

TMP="$(mktemp "${TMPDIR:-/tmp}/raii-scan.XXXXXX")"
trap 'rm -f "$TMP"' EXIT

TOTAL_METHODS=0
TOTAL_HANDLES=0

echo -e "${BOLD}╔══════════════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BOLD}║                       RAII Ratchet (src/ headers)                        ║${NC}"
echo -e "${BOLD}╚══════════════════════════════════════════════════════════════════════════╝${NC}"
echo ""
printf "${CYAN}%-60s %10s %10s${NC}\n" "Header" "lifecycle" "handles"
printf "%-60s %10s %10s\n" "------------------------------------------------------------" "----------" "----------"

while IFS= read -r file; do
    rel="${file#$PROJECT_ROOT/}"
    scan_header "$file" > "$TMP"
    m=$(grep -c '^M ' "$TMP" || true)
    h=$(grep -c '^H ' "$TMP" || true)
    if [[ "$m" -eq 0 && "$h" -eq 0 ]]; then
        continue
    fi
    TOTAL_METHODS=$((TOTAL_METHODS + m))
    TOTAL_HANDLES=$((TOTAL_HANDLES + h))
    printf "%-60s %10d %10d\n" "$rel" "$m" "$h"
    if [[ "$VERBOSE" == "true" ]]; then
        while IFS= read -r hit; do
            kind="${hit%% *}"
            rest="${hit#* }"
            ln="${rest%% *}"
            text="${rest#* }"
            label="handle"
            [[ "$kind" == "M" ]] && label="method"
            printf "    ${YELLOW}[%s]${NC} %s:%s: %s\n" "$label" "$rel" "$ln" "$(echo "$text" | sed 's/^[[:space:]]*//')"
        done < "$TMP"
    fi
done < <(find "$SRC_DIR" -name "*.h" -not -path "$SRC_DIR/core/vulkan/*" | sort)

printf "%-60s %10s %10s\n" "------------------------------------------------------------" "----------" "----------"
printf "${BOLD}%-60s %10d %10d${NC}\n" "TOTAL" "$TOTAL_METHODS" "$TOTAL_HANDLES"
echo ""
echo "lifecycle_methods=$TOTAL_METHODS"
echo "raw_handle_members=$TOTAL_HANDLES"
echo ""

if [[ "$UPDATE" == "true" ]]; then
    {
        echo "# RAII ratchet baseline. Generated by scripts/analyze-raii.sh --update."
        echo "# These numbers may only go down; lower them in the same commit that reduces the counts."
        echo "lifecycle_methods=$TOTAL_METHODS"
        echo "raw_handle_members=$TOTAL_HANDLES"
    } > "$BASELINE"
    echo -e "${GREEN}Baseline written to ${BASELINE#$PROJECT_ROOT/}${NC}"
fi

if [[ "$CHECK" == "true" ]]; then
    if [[ ! -f "$BASELINE" ]]; then
        echo -e "${RED}No baseline found at $BASELINE. Run $0 --update to create it.${NC}"
        exit 1
    fi
    BASE_METHODS=$(sed -n 's/^lifecycle_methods=\([0-9]*\)$/\1/p' "$BASELINE")
    BASE_HANDLES=$(sed -n 's/^raw_handle_members=\([0-9]*\)$/\1/p' "$BASELINE")
    if [[ -z "$BASE_METHODS" || -z "$BASE_HANDLES" ]]; then
        echo -e "${RED}Baseline $BASELINE is malformed. Run $0 --update to regenerate it.${NC}"
        exit 1
    fi
    FAIL=0
    if [[ "$TOTAL_METHODS" -gt "$BASE_METHODS" ]]; then
        echo -e "${RED}FAIL: public lifecycle methods rose from $BASE_METHODS to $TOTAL_METHODS${NC}"
        echo -e "  ${GREEN}→ Do not add init/initialize/cleanup/destroy/shutdown; construct fully in the constructor and release in the destructor${NC}"
        FAIL=1
    fi
    if [[ "$TOTAL_HANDLES" -gt "$BASE_HANDLES" ]]; then
        echo -e "${RED}FAIL: raw Vulkan/VMA handle members rose from $BASE_HANDLES to $TOTAL_HANDLES${NC}"
        echo -e "  ${GREEN}→ Use the RAII wrappers in src/core/vulkan/ (VmaBuffer, VmaImage, vk::Unique*)${NC}"
        FAIL=1
    fi
    if [[ "$FAIL" -ne 0 ]]; then
        echo -e "  Run $0 --verbose to list the offending lines."
        exit 1
    fi
    if [[ "$TOTAL_METHODS" -lt "$BASE_METHODS" || "$TOTAL_HANDLES" -lt "$BASE_HANDLES" ]]; then
        echo -e "${YELLOW}Counts are below the baseline ($BASE_METHODS/$BASE_HANDLES). Lower it: $0 --update${NC}"
    fi
    echo -e "${GREEN}OK: lifecycle methods $TOTAL_METHODS <= $BASE_METHODS, raw handle members $TOTAL_HANDLES <= $BASE_HANDLES${NC}"
fi

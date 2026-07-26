#!/bin/bash
# Main-Thread Stall Analysis Script
# Audits the "Threading and Loading Design Principles" in CLAUDE.md:
# the presenting thread must never run long loading/initialization work.
#
# Usage: ./scripts/analyze-main-thread-stalls.sh [--verbose] [--strict]
#   --verbose  Show every offending line
#   --strict   Exit nonzero if any violations are found (for CI)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SRC_DIR="$PROJECT_ROOT/src"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

VERBOSE=false
STRICT=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --verbose|-v) VERBOSE=true; shift ;;
        --strict|-s)  STRICT=true; shift ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [--verbose] [--strict]"
            exit 1
            ;;
    esac
done

VIOLATIONS=0
REVIEWS=0

section() {
    echo -e "${CYAN}┌──────────────────────────────────────────────────────────────────────────┐${NC}"
    printf "${CYAN}│${NC} %-72s ${CYAN}│${NC}\n" "$1"
    echo -e "${CYAN}└──────────────────────────────────────────────────────────────────────────┘${NC}"
    echo ""
}

echo -e "${BOLD}╔══════════════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BOLD}║              Main-Thread Stall Analysis (loading / init)                 ║${NC}"
echo -e "${BOLD}╚══════════════════════════════════════════════════════════════════════════╝${NC}"
echo ""

# ============================================================================
# Section 1: Heavy work inside gpuWork lambdas (VIOLATION)
#
# gpuWork runs on the main thread inside AsyncSystemLoader::pollCompletions().
# It must only adopt/wire objects built by workers. Any creation, compilation,
# or IO call inside a gpuWork body is a stall on the presenting thread.
# ============================================================================
section "Section 1: Heavy work inside SystemInitTask::gpuWork (VIOLATION)"

GPU_WORK_HITS=$(find "$SRC_DIR" \( -name "*.cpp" -o -name "*.h" \) -print0 | xargs -0 awk '
    /gpuWork[[:space:]]*=[[:space:]]*\[/ { inbody=1; depth=0; opened=0 }
    inbody {
        line=$0
        if (line ~ /::create\(|::createAll\(|createWithDependencies|createGraphicsPipeline|createComputePipeline|vkCreate|loadShaderModule|std::ifstream|readFile\(|stbi_load|SDL_LoadBMP/ \
            && line !~ /^[[:space:]]*\/\//) print FILENAME ":" FNR ": " line
        n=split(line, a, "{"); depth+=n-1; if (n>1) opened=1
        m=split(line, b, "}"); depth-=m-1
        if (opened && depth<=0) inbody=0
    }
' || true)

if [[ -n "$GPU_WORK_HITS" ]]; then
    COUNT=$(echo "$GPU_WORK_HITS" | wc -l | tr -d ' ')
    VIOLATIONS=$((VIOLATIONS + COUNT))
    echo -e "${RED}Found $COUNT heavy call(s) inside gpuWork bodies${NC}"
    echo -e "  ${GREEN}→ Move to cpuWork (worker thread); gpuWork should only adopt/wire results${NC}"
    if [[ "$VERBOSE" == "true" ]]; then
        echo "$GPU_WORK_HITS" | sed 's/^/  /'
    else
        echo "$GPU_WORK_HITS" | cut -d: -f1 | sort | uniq -c | sed 's/^/  /'
    fi
else
    echo -e "${GREEN}No heavy work found inside gpuWork bodies${NC}"
fi
echo ""

# ============================================================================
# Section 2: Rendering / event pumping inside callbacks (VIOLATION)
#
# Progress and yield callbacks must only publish progress state. Calling
# render(), SDL_PumpEvents(), or SDL_Delay() from inside init code makes the
# spinner cadence depend on init code volunteering control.
# ============================================================================
section "Section 2: render()/pump/delay inside progress callbacks (VIOLATION)"

CALLBACK_HITS=$(find "$SRC_DIR" \( -name "*.cpp" -o -name "*.h" \) -print0 | xargs -0 awk '
    /(progressCallback|yieldCallback)[[:space:]]*=[[:space:]]*\[/ { inbody=1; depth=0; opened=0 }
    inbody {
        line=$0
        if (line ~ /->render\(|SDL_PumpEvents|SDL_Delay/ && line !~ /^[[:space:]]*\/\//) print FILENAME ":" FNR ": " line
        n=split(line, a, "{"); depth+=n-1; if (n>1) opened=1
        m=split(line, b, "}"); depth-=m-1
        if (opened && depth<=0) inbody=0
    }
' 2>/dev/null || true)

if [[ -n "$CALLBACK_HITS" ]]; then
    COUNT=$(echo "$CALLBACK_HITS" | wc -l | tr -d ' ')
    VIOLATIONS=$((VIOLATIONS + COUNT))
    echo -e "${RED}Found $COUNT render/pump/delay call(s) inside progress/yield callbacks${NC}"
    echo -e "  ${GREEN}→ Callbacks should only record progress; the loading loop owns rendering${NC}"
    echo "$CALLBACK_HITS" | sed 's/^/  /'
else
    echo -e "${GREEN}No rendering or event pumping inside progress callbacks${NC}"
fi
echo ""

# ============================================================================
# Section 3: yieldCallback plumbing (VIOLATION)
#
# yieldCallback exists to hand control back to the spinner from inside long
# main-thread work. Under the design principles that work runs on a worker
# thread instead, so yieldCallback plumbing should disappear entirely.
# ============================================================================
section "Section 3: yieldCallback plumbing (VIOLATION)"

YIELD_FILES=$(grep -rl "yieldCallback" "$SRC_DIR" --include="*.cpp" --include="*.h" 2>/dev/null || true)

if [[ -n "$YIELD_FILES" ]]; then
    COUNT=$(echo "$YIELD_FILES" | wc -l | tr -d ' ')
    VIOLATIONS=$((VIOLATIONS + COUNT))
    echo -e "${RED}yieldCallback found in $COUNT file(s)${NC}"
    echo -e "  ${GREEN}→ Move the surrounding work to a worker thread and delete the yield plumbing${NC}"
    echo "$YIELD_FILES" | sed 's/^/  /'
    if [[ "$VERBOSE" == "true" ]]; then
        grep -rn "yieldCallback" $YIELD_FILES 2>/dev/null | sed 's/^/  /'
    fi
else
    echo -e "${GREEN}No yieldCallback plumbing found${NC}"
fi
echo ""

# ============================================================================
# Section 4: Blocking calls on main-thread paths (REVIEW)
#
# Not automatically wrong (shutdown/resize legitimately use waitIdle), but
# every hit should be justified. sleep/delay in loading loops caps frame rate;
# waitIdle in a per-frame or loading path is a full pipeline stall.
# ============================================================================
section "Section 4: Blocking calls - waitIdle / SDL_Delay / sleep_for (REVIEW)"

for PATTERN in "waitIdle" "SDL_Delay" "sleep_for"; do
    HITS=$(grep -rn "$PATTERN" "$SRC_DIR" --include="*.cpp" --include="*.h" 2>/dev/null | grep -v "^\s*//" || true)
    if [[ -n "$HITS" ]]; then
        COUNT=$(echo "$HITS" | wc -l | tr -d ' ')
        REVIEWS=$((REVIEWS + COUNT))
        echo -e "${YELLOW}$PATTERN${NC} found ${BOLD}$COUNT${NC} time(s)"
        if [[ "$VERBOSE" == "true" ]]; then
            echo "$HITS" | sed 's/^/  /'
        else
            echo "$HITS" | cut -d: -f1 | sort | uniq -c | sed 's/^/  /'
        fi
        echo ""
    fi
done

# ============================================================================
# Section 5: Blocking loading loops (REVIEW)
#
# runLoadingLoop() blocks its caller until loading completes. Acceptable only
# when the caller is a dedicated loading loop that renders each iteration.
# ============================================================================
section "Section 5: Blocking loading loops (REVIEW)"

LOOP_HITS=$(grep -rn "runLoadingLoop\(\)" "$SRC_DIR" --include="*.cpp" 2>/dev/null | grep -v "^\s*//" | grep -v "void .*runLoadingLoop" || true)
if [[ -n "$LOOP_HITS" ]]; then
    COUNT=$(echo "$LOOP_HITS" | wc -l | tr -d ' ')
    REVIEWS=$((REVIEWS + COUNT))
    echo -e "${YELLOW}runLoadingLoop() called $COUNT time(s) - verify each renders every iteration${NC}"
    echo "$LOOP_HITS" | sed 's/^/  /'
else
    echo -e "${GREEN}No blocking runLoadingLoop() call sites${NC}"
fi
echo ""

# ============================================================================
# Summary
# ============================================================================
echo -e "${BOLD}Summary:${NC}"
echo -e "  Violations (must fix):   ${RED}$VIOLATIONS${NC}"
echo -e "  Review items (justify):  ${YELLOW}$REVIEWS${NC}"
echo ""
echo -e "Design principles: see ${BOLD}CLAUDE.md${NC} - Threading and Loading Design Principles"
echo -e "Runtime audit: InitProfiler warns when a main-thread init phase exceeds its budget"

if [[ "$STRICT" == "true" && $VIOLATIONS -gt 0 ]]; then
    exit 1
fi

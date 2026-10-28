#!/usr/bin/env bash
# Verify a C48 public-source build against the authenticated C47/C44 references.
#
# Usage:
#   REFERENCE_OUT=<C44 reference out directory> ./verify-build.sh
#
# The public snapshot intentionally omits one proprietary private wrapper and
# four Ulefone board modules that embed vendor firmware/calibration images.
# This script checks those exclusions explicitly instead of hiding them.
#
# Reproducibility levels:
#   - Image, vmlinux, System.map: exact SHA256 match.
#   - 699 in-tree modules: byte-identical to the C44 reference.
#   - panel, mtk-mmc: code-identical to the authenticated C45/C46 packaged
#     modules after debug-info removal and build-id removal.
#   - apusys: code-identical to the C44 reference after the C48 MDLA link-order
#     pin; only local symbol/string tables differ before normalization.
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(git -C "$script_dir" rev-parse --show-toplevel)
out=${OUT:-$repo_root/out/c48}
ref=${REFERENCE_OUT:-}
manifest=${MANIFEST:-$script_dir/configs/c47-reference-sha256.txt}
normalized=${NORMALIZED:-$script_dir/configs/c48-module-normalized.txt}
symvers_ref=${SYMVERS_REF:-$script_dir/configs/c47-reference-symvers.txt}
exclusions=${EXCLUSIONS:-$script_dir/configs/publication-exclusions.txt}
expected_modules=702
fail=0

fail_msg() {
	printf 'FAIL     %s\n' "$*"
	fail=1
}

hash_of() {
	sha256sum "$1" | awk '{print $1}'
}

normalized_hash() {
	local src=$1 tmp
	tmp=$(mktemp "${TMPDIR:-/tmp}/c48-module.XXXXXX")
	cp "$src" "$tmp"
	llvm-strip --strip-debug "$tmp"
	llvm-objcopy --remove-section .note.gnu.build-id "$tmp" "$tmp"
	hash_of "$tmp"
	rm -f "$tmp"
}

check_one() {
	local key=$1 rel=$2 expected=$3
	local path="$out/$rel"
	if [[ ! -f $path ]]; then
		fail_msg "$key missing: $rel"
		return
	fi
	local actual
	actual=$(hash_of "$path")
	if [[ $actual == "$expected" ]]; then
		printf 'OK       %-28s %s\n' "$key" "$rel"
	else
		printf 'DIFFER   %-28s %s\n  expected %s\n  actual   %s\n' \
			"$key" "$rel" "$expected" "$actual"
		fail=1
	fi
}

if [[ ! -d $out ]]; then
	printf 'output directory not found: %s\n' "$out" >&2
	exit 2
fi
for tool in llvm-strip llvm-objcopy sha256sum; do
	command -v "$tool" >/dev/null 2>&1 || {
		printf 'missing required tool: %s\n' "$tool" >&2
		exit 2
	}
done

printf '== exact kernel artifacts ==\n'
while IFS=$'\t' read -r key rel expected; do
	[[ -z ${key:-} || $key == \#* ]] && continue
	check_one "$key" "$rel" "$expected"
done < "$manifest"

printf '\n== configuration ==\n'
for symbol in CONFIG_CFI_CLANG CONFIG_MODULES CONFIG_MODVERSIONS; do
	if grep -q "^${symbol}=y$" "$out/.config"; then
		printf 'OK       %s=y\n' "$symbol"
	else
		fail_msg "$symbol=y"
	fi
done
release=$(cat "$out/include/config/kernel.release" 2>/dev/null || true)
if [[ $release == "6.1.115-mt6878-gq5012bf1-c29+" ]]; then
	printf 'OK       kernel release %s\n' "$release"
else
	fail_msg "kernel release: ${release:-<missing>}"
fi

printf '\n== intentional publication exclusions ==\n'
while IFS=$'\t' read -r rel reason; do
	[[ -z ${rel:-} || $rel == \#* ]] && continue
	if [[ -e $out/$rel ]]; then
		fail_msg "excluded module was built: $rel ($reason)"
	else
		printf 'OK       absent: %-58s %s\n' "$rel" "$reason"
	fi
done < "$exclusions"

mapfile -t modules < <(cd "$out" && find . -name '*.ko' -type f | sed 's#^\./##' | sort)
count=${#modules[@]}
if [[ $count -eq $expected_modules ]]; then
	printf 'OK       in-tree module count: %d\n' "$count"
else
	fail_msg "in-tree module count: $count (expected $expected_modules)"
fi

printf '\n== module reproducibility ==\n'
declare -A norm_expect=()
while IFS=$'\t' read -r rel hash; do
	[[ -z ${rel:-} || $rel == \#* ]] && continue
	norm_expect[$rel]=$hash
done < "$normalized"
exact=0
normalized_count=0
different=0
for rel in "${modules[@]}"; do
	if [[ -n ${norm_expect[$rel]:-} ]]; then
		actual=$(normalized_hash "$out/$rel")
		if [[ $actual == "${norm_expect[$rel]}" ]]; then
			printf 'OK       normalized %-46s %s\n' "$rel" "${actual:0:16}..."
			normalized_count=$((normalized_count + 1))
		else
			printf 'DIFFER   normalized %s\n  expected %s\n  actual   %s\n' \
				"$rel" "${norm_expect[$rel]}" "$actual"
			different=$((different + 1))
		fi
	elif [[ -n $ref ]]; then
		if [[ ! -f $ref/$rel ]]; then
			printf 'REF-MISSING %s\n' "$rel"
			different=$((different + 1))
		elif cmp -s "$out/$rel" "$ref/$rel"; then
			exact=$((exact + 1))
		else
			printf 'DIFFER   %s\n' "$rel"
			different=$((different + 1))
		fi
	fi
done
printf 'modules: %d byte-identical, %d normalized-identical, %d different\n' \
	"$exact" "$normalized_count" "$different"
[[ $different -eq 0 ]] || fail=1

if [[ -n $ref ]]; then
	printf '\n== reference-only modules ==\n'
	if [[ ! -d $ref ]]; then
		printf 'reference out directory not found: %s\n' "$ref" >&2
		exit 2
	fi
	(cd "$ref" && find . -name '*.ko' -type f | sort) > /tmp/c48-ref-modules.$$
	(cd "$out" && find . -name '*.ko' -type f | sort) > /tmp/c48-out-modules.$$
	ref_only=/tmp/c48-ref-only.$$
	comm -23 /tmp/c48-ref-modules.$$ /tmp/c48-out-modules.$$ > "$ref_only"
	printf 'reference-only modules:\n'
	sed 's/^/  /' "$ref_only"
	expected_ref_only=$(awk -F'\t' '!/^#/ && NF {sub(/^\.\//,"",$1); print "./"$1}' "$exclusions" | sort)
	if diff -u <(printf '%s\n' "$expected_ref_only") "$ref_only" >/tmp/c48-ref-only-diff.$$; then
		printf 'OK       reference-only set is exactly the five documented exclusions\n'
	else
		fail_msg 'reference-only module set differs from the documented exclusions'
		cat /tmp/c48-ref-only-diff.$$
	fi

	printf '\n== symvers delta ==\n'
	while IFS=$'\t' read -r name expected; do
		[[ -z ${name:-} || $name == \#* ]] && continue
		if [[ $name == "Module.symvers" || $name == "modules-only.symvers" ]]; then
			if [[ ! -f $out/$name ]]; then
				fail_msg "$name missing"
				continue
			fi
			if diff -u "$ref/$name" "$out/$name" >/tmp/c48-symvers-diff.$$; then
				printf 'OK       %s is bit-identical to reference\n' "$name"
			else
				removed=$(grep -c '^-0x' /tmp/c48-symvers-diff.$$ || true)
				added=$(grep -c '^+0x' /tmp/c48-symvers-diff.$$ || true)
				if [[ $removed -eq 2 && $added -eq 0 ]] && \
					grep -q 'tiny_tp_gesture_contorl' /tmp/c48-symvers-diff.$$ && \
					grep -q 'tiny_tp_power_contorl' /tmp/c48-symvers-diff.$$; then
					printf 'OK       %s differs by exactly the two excluded Hynitron exports\n' "$name"
				else
					fail_msg "$name delta is not the documented two-line exclusion"
					cat /tmp/c48-symvers-diff.$$
				fi
			fi
			rm -f /tmp/c48-symvers-diff.$$
		fi
	done < "$symvers_ref"
	rm -f /tmp/c48-ref-modules.$$ /tmp/c48-out-modules.$$ /tmp/c48-ref-only.$$ \
		/tmp/c48-ref-only-diff.$$
else
	printf '\nREFERENCE_OUT not set: skipped reference-only and symvers comparison\n'
fi

printf '\n== result ==\n'
if [[ $fail -eq 0 ]]; then
	printf 'PASS: kernel artifacts, module code and documented exclusions match\n'
else
	printf 'FAIL: at least one checked artifact differs or is missing\n'
	exit 1
fi

#!/bin/sh
# Reclaim PulseAudio null sinks left by dead Phase 6 test owners.
set -efu

if ! command -v pactl >/dev/null 2>&1; then
	echo "phase6-audio-orphans: SKIP: pactl unavailable"
	exit 0
fi

modules=$(pactl list short modules) || {
	echo "phase6-audio-orphans: cannot list PulseAudio modules" >&2
	exit 1
}

tab=$(printf '\t')
printf '%s\n' "$modules" |
while IFS="$tab" read -r module module_name arguments; do
	[ "$module_name" = "module-null-sink" ] || continue
	case "$module" in
	""|*[!0-9]*) continue ;;
	esac

	sink_name=""
	for argument in $arguments; do
		case "$argument" in
		sink_name=*) sink_name=${argument#sink_name=} ;;
		esac
	done
	case "$sink_name" in
	omaq_p6_*) ;;
	*) continue ;;
	esac

	owner_and_suffix=${sink_name#omaq_p6_}
	owner=${owner_and_suffix%%_*}
	suffix=${owner_and_suffix#"$owner"_}
	case "$owner" in
	""|*[!0-9]*) continue ;;
	esac
	case "$suffix" in
	cap_a|out_a|cap_b|out_b) ;;
	*) continue ;;
	esac

	# A live PID may be the Phase 6 owner or a safely retained PID reuse.
	# /proc also treats an EPERM result from kill -0 as live.
	if kill -0 "$owner" 2>/dev/null || [ -d "/proc/$owner" ]; then
		continue
	fi
	if ! pactl unload-module "$module"; then
		after=$(pactl list short modules) || {
			echo "phase6-audio-orphans: cannot confirm stale module removal" >&2
			exit 1
		}
		if printf '%s\n' "$after" | awk -v wanted="$module" '
			$1 == wanted { found = 1 }
			END { exit found ? 0 : 1 }
		'; then
			echo "phase6-audio-orphans: failed to unload stale module $module" >&2
			exit 1
		fi
	fi
done
